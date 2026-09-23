#include "archive_extract.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <array>
#include <algorithm>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

namespace vibescriber {
namespace {

struct ArchiveDeleter
{
    void operator()(archive* handle) const
    {
        archive_read_free(handle);
    }
};

class PartialFile
{
public:
    explicit PartialFile(std::filesystem::path path)
        : path_(std::move(path))
    {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    ~PartialFile()
    {
        if (!committed_) {
            std::error_code error;
            std::filesystem::remove(path_, error);
        }
    }

    PartialFile(const PartialFile&) = delete;
    PartialFile& operator=(const PartialFile&) = delete;

    void commit()
    {
        committed_ = true;
    }

private:
    std::filesystem::path path_;
    bool committed_ = false;
};

[[nodiscard]] std::string archive_message(
    archive* handle,
    const std::string& context)
{
    const char* details = archive_error_string(handle);
    if (details == nullptr) {
        return context;
    }
    return context + ": " + details;
}

void require_archive_success(
    archive* handle,
    const int result,
    const std::string& context)
{
    if (result < ARCHIVE_WARN) {
        throw ArchiveError(archive_message(handle, context));
    }
}

[[nodiscard]] bool is_safe_member_path(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute()) {
        return false;
    }
    for (const auto& component : path) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

} // namespace

void extract_archive_member(
    const std::filesystem::path& archive_path,
    const std::filesystem::path& member_path,
    const std::filesystem::path& destination)
{
    if (!std::filesystem::is_regular_file(archive_path)) {
        throw ArchiveError("the archive does not exist: " + archive_path.string());
    }
    if (!is_safe_member_path(member_path)) {
        throw ArchiveError("the requested archive member must be a safe relative path");
    }
    if (destination.empty()) {
        throw ArchiveError("the extraction destination may not be empty");
    }

    const auto parent = destination.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    std::filesystem::path staging_path = destination;
    staging_path += ".part";
    PartialFile partial_file(staging_path);

    std::unique_ptr<archive, ArchiveDeleter> reader(archive_read_new());
    if (!reader) {
        throw ArchiveError("failed to create an archive reader");
    }

    require_archive_success(
        reader.get(), archive_read_support_filter_all(reader.get()),
        "failed to enable archive decompression filters");
    require_archive_success(
        reader.get(), archive_read_support_format_all(reader.get()),
        "failed to enable archive formats");
    require_archive_success(
        reader.get(),
        archive_read_open_filename(reader.get(), archive_path.string().c_str(), 64U * 1024U),
        "failed to open the archive");

    bool found = false;
    archive_entry* entry = nullptr;
    int result = ARCHIVE_OK;
    while ((result = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
        const char* raw_path = archive_entry_pathname(entry);
        const std::string entry_path = raw_path == nullptr ? std::string{} : raw_path;
        if (entry_path != member_path.generic_string()) {
            require_archive_success(
                reader.get(), archive_read_data_skip(reader.get()),
                "failed to skip an archive member");
            continue;
        }

        if (found) {
            throw ArchiveError("the archive contains the requested member more than once");
        }
        if (archive_entry_filetype(entry) != AE_IFREG) {
            throw ArchiveError("the requested archive member is not a regular file");
        }

        std::ofstream output(staging_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw ArchiveError("could not open the temporary extraction file: "
                               + staging_path.string());
        }

        std::array<char, 64U * 1024U> buffer{};
        for (;;) {
            const la_ssize_t bytes = archive_read_data(
                reader.get(), buffer.data(), buffer.size());
            if (bytes == 0) {
                break;
            }
            if (bytes < 0) {
                throw ArchiveError(archive_message(
                    reader.get(), "failed while reading the archive member"));
            }
            output.write(buffer.data(), static_cast<std::streamsize>(bytes));
            if (!output) {
                throw ArchiveError("failed while writing the extracted file");
            }
        }
        output.close();
        if (!output) {
            throw ArchiveError("failed to finish writing the extracted file");
        }
        found = true;
    }

    if (result != ARCHIVE_EOF) {
        throw ArchiveError(archive_message(reader.get(), "failed while reading the archive"));
    }
    if (!found) {
        throw ArchiveError("the requested member was not found in the archive: "
                           + member_path.generic_string());
    }

    std::error_code error;
    if (std::filesystem::exists(destination, error)) {
        error.clear();
        if (!std::filesystem::remove(destination, error) || error) {
            throw ArchiveError("could not replace the extraction destination: "
                               + destination.string());
        }
    } else if (error) {
        throw ArchiveError("could not inspect the extraction destination: "
                           + destination.string());
    }

    std::filesystem::rename(staging_path, destination, error);
    if (error) {
        throw ArchiveError("could not move the extracted file into place: "
                           + destination.string());
    }
    partial_file.commit();
}

void extract_archive_directory(
    const std::filesystem::path& archive_path,
    const std::filesystem::path& destination,
    const bool strip_first_component)
{
    if (!std::filesystem::is_regular_file(archive_path) || destination.empty()) {
        throw ArchiveError("invalid archive or extraction destination");
    }
    std::filesystem::path staging = destination;
    staging += ".part";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(destination.parent_path());
    std::filesystem::create_directories(staging);
    try {
        std::unique_ptr<archive, ArchiveDeleter> reader(archive_read_new());
        require_archive_success(reader.get(), archive_read_support_filter_all(reader.get()),
                                "failed to enable archive filters");
        require_archive_success(reader.get(), archive_read_support_format_all(reader.get()),
                                "failed to enable archive formats");
        require_archive_success(reader.get(), archive_read_open_filename(
            reader.get(), archive_path.string().c_str(), 64U * 1024U),
            "failed to open archive");
        archive_entry* entry = nullptr;
        int result = ARCHIVE_OK;
        while ((result = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
            std::string raw = archive_entry_pathname(entry);
            std::replace(raw.begin(), raw.end(), '\\', '/');
            std::filesystem::path relative(raw);
            if (!is_safe_member_path(relative)) {
                throw ArchiveError("archive contains an unsafe path");
            }
            if (strip_first_component) {
                auto component = relative.begin();
                ++component;
                std::filesystem::path stripped;
                for (; component != relative.end(); ++component) {
                    stripped /= *component;
                }
                relative = stripped;
            }
            if (relative.empty()) {
                continue;
            }
            const auto output = staging / relative;
            const auto type = archive_entry_filetype(entry);
            if (type == AE_IFDIR) {
                std::filesystem::create_directories(output);
            } else if (type == AE_IFREG) {
                std::filesystem::create_directories(output.parent_path());
                std::ofstream file(output, std::ios::binary | std::ios::trunc);
                if (!file) {
                    throw ArchiveError("could not create extracted file");
                }
                std::array<char, 64U * 1024U> buffer{};
                for (;;) {
                    const auto bytes = archive_read_data(reader.get(), buffer.data(), buffer.size());
                    if (bytes == 0) break;
                    if (bytes < 0) throw ArchiveError(archive_message(reader.get(), "archive read failed"));
                    file.write(buffer.data(), static_cast<std::streamsize>(bytes));
                    if (!file) throw ArchiveError("archive write failed");
                }
#ifndef _WIN32
                if ((archive_entry_perm(entry) & 0111) != 0) {
                    std::filesystem::permissions(output, std::filesystem::perms::owner_exec
                        | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
                        std::filesystem::perm_options::add);
                }
#endif
            } else if (type == AE_IFLNK) {
                const char* link = archive_entry_symlink(entry);
                if (link == nullptr || !is_safe_member_path(std::filesystem::path(link))) {
                    throw ArchiveError("archive contains an unsafe symbolic link");
                }
                std::filesystem::create_directories(output.parent_path());
                std::filesystem::create_symlink(link, output);
            } else {
                throw ArchiveError("archive contains an unsupported entry");
            }
        }
        if (result != ARCHIVE_EOF) {
            throw ArchiveError(archive_message(reader.get(), "archive read failed"));
        }
        std::filesystem::remove_all(destination);
        std::filesystem::rename(staging, destination);
    } catch (...) {
        std::error_code error;
        std::filesystem::remove_all(staging, error);
        throw;
    }
}

} // namespace vibescriber
