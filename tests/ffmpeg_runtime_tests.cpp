#include "archive_extract.hpp"
#include "ffmpeg_runtime.hpp"
#include "sha256.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace {

class TestSuite
{
public:
    void expect(const bool condition, const std::string_view description)
    {
        if (!condition) {
            ++failures_;
            std::cerr << "FAIL: " << description << '\n';
        }
    }

    template <typename Exception, typename Function>
    void expect_error(Function&& function, const std::string_view description)
    {
        try {
            function();
            expect(false, description);
        } catch (const Exception&) {
        }
    }

    [[nodiscard]] int failures() const
    {
        return failures_;
    }

private:
    int failures_ = 0;
};

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        const auto unique_value = std::chrono::steady_clock::now()
                                      .time_since_epoch()
                                      .count();
        path_ = std::filesystem::temp_directory_path()
                / ("vibescriber-ffmpeg-tests-" + std::to_string(unique_value));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

struct ArchiveWriterDeleter
{
    void operator()(archive* writer) const
    {
        archive_write_free(writer);
    }
};

struct ArchiveEntryDeleter
{
    void operator()(archive_entry* entry) const
    {
        archive_entry_free(entry);
    }
};

void require_archive_success(archive* writer, const int result)
{
    if (result != ARCHIVE_OK) {
        const char* details = archive_error_string(writer);
        throw std::runtime_error(details == nullptr ? "archive operation failed" : details);
    }
}

void add_file(
    archive* writer,
    const std::string_view path,
    const std::string_view contents)
{
    std::unique_ptr<archive_entry, ArchiveEntryDeleter> entry(archive_entry_new());
    if (!entry) {
        throw std::runtime_error("failed to create a test archive entry");
    }

    const std::string path_string(path);
    archive_entry_set_pathname(entry.get(), path_string.c_str());
    archive_entry_set_filetype(entry.get(), AE_IFREG);
    archive_entry_set_perm(entry.get(), 0644);
    archive_entry_set_size(entry.get(), static_cast<la_int64_t>(contents.size()));
    require_archive_success(writer, archive_write_header(writer, entry.get()));

    const la_ssize_t written = archive_write_data(
        writer, contents.data(), contents.size());
    if (written != static_cast<la_ssize_t>(contents.size())) {
        throw std::runtime_error("failed to write a test archive entry");
    }
}

void create_zip(
    const std::filesystem::path& path,
    const std::string_view member,
    const std::string_view contents)
{
    std::unique_ptr<archive, ArchiveWriterDeleter> writer(archive_write_new());
    if (!writer) {
        throw std::runtime_error("failed to create a test archive writer");
    }

    require_archive_success(writer.get(), archive_write_set_format_zip(writer.get()));
    require_archive_success(
        writer.get(), archive_write_open_filename(writer.get(), path.string().c_str()));
    add_file(writer.get(), "unrelated/readme.txt", "not FFmpeg");
    add_file(writer.get(), member, contents);
    require_archive_success(writer.get(), archive_write_close(writer.get()));
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::string file_url(const std::filesystem::path& path)
{
#ifdef _WIN32
    return "file:///" + path.generic_string();
#else
    return "file://" + path.generic_string();
#endif
}

void test_pinned_packages(TestSuite& suite)
{
    const auto& windows = vibescriber::ffmpeg_package(
        vibescriber::OperatingSystem::windows_host);
    suite.expect(
        windows.archive.url.find("autobuild-2026-09-17-13-19") != std::string::npos,
        "the Windows package is pinned to an immutable release");
    suite.expect(
        windows.archive.sha256
            == "c740a325e7eeeddd373764cd274b50768190b4030a726fe938dca93f968dfc10",
        "the Windows package has its published SHA-256");
    suite.expect(windows.archive.expected_bytes == 169'508'589U,
                 "the Windows package has its published size");
    suite.expect(windows.archive_member.filename() == "ffmpeg.exe",
                 "the Windows package selects only ffmpeg.exe");

    const auto& linux_package = vibescriber::ffmpeg_package(
        vibescriber::OperatingSystem::linux_host);
    suite.expect(
        linux_package.archive.url.find("autobuild-2026-09-17-13-19")
            != std::string::npos,
        "the Linux package is pinned to an immutable release");
    suite.expect(
        linux_package.archive.sha256
            == "3846384ed094486b54d44b05cdc2076a34607295cfdcce612efd6e4db2319249",
        "the Linux package has its published SHA-256");
    suite.expect(linux_package.archive.expected_bytes == 136'031'360U,
                 "the Linux package has its published size");
    suite.expect(linux_package.archive_member.filename() == "ffmpeg",
                 "the Linux package selects only ffmpeg");
}

void test_member_extraction(TestSuite& suite)
{
    TemporaryDirectory temporary_directory;
    const auto archive_path = temporary_directory.path() / "fixture.zip";
    const auto destination = temporary_directory.path() / "tools" / "ffmpeg";
    create_zip(archive_path, "package/bin/ffmpeg", "fixture executable");

    vibescriber::extract_archive_member(
        archive_path, "package/bin/ffmpeg", destination);
    suite.expect(read_text(destination) == "fixture executable",
                 "the requested nested archive member is extracted");
    suite.expect(!std::filesystem::exists(destination.string() + ".part"),
                 "successful extraction leaves no staging file");

    const auto missing_destination = temporary_directory.path() / "missing";
    suite.expect_error<vibescriber::ArchiveError>(
        [&] {
            vibescriber::extract_archive_member(
                archive_path, "package/bin/missing", missing_destination);
        },
        "a missing archive member is rejected");
    suite.expect(!std::filesystem::exists(missing_destination),
                 "a failed extraction creates no destination");
    suite.expect(!std::filesystem::exists(missing_destination.string() + ".part"),
                 "a failed extraction removes its staging file");
}

void test_ffmpeg_install(TestSuite& suite)
{
    TemporaryDirectory temporary_directory;
    const auto source_archive = temporary_directory.path() / "source.zip";
    const auto data_directory = temporary_directory.path() / "data";
    constexpr std::string_view member = "fixture/bin/ffmpeg";
    create_zip(source_archive, member, "local ffmpeg fixture");

    const vibescriber::FfmpegPackage fixture{
        .archive = {
            .display_name = "test FFmpeg package",
            .url = file_url(source_archive),
            .sha256 = vibescriber::sha256_file(source_archive),
            .relative_path = "downloads/fixture.zip",
            .expected_bytes = std::filesystem::file_size(source_archive),
        },
        .archive_member = member,
        .executable_relative_path = "tools/ffmpeg",
        .set_executable_permissions = true,
    };

    const auto result = vibescriber::ensure_ffmpeg(data_directory, fixture);
    suite.expect(result.status == vibescriber::FfmpegInstallStatus::installed,
                 "a verified FFmpeg package is installed");
    suite.expect(read_text(result.executable_path) == "local ffmpeg fixture",
                 "the installed FFmpeg file has the archive contents");
    suite.expect(!std::filesystem::exists(data_directory / "downloads/fixture.zip"),
                 "the verified archive is removed after installation");
    const auto permissions = std::filesystem::status(result.executable_path).permissions();
    suite.expect((permissions & std::filesystem::perms::owner_exec)
                     != std::filesystem::perms::none,
                 "the installed Linux-style helper is executable");

    const auto second_result = vibescriber::ensure_ffmpeg(data_directory, fixture);
    suite.expect(second_result.status == vibescriber::FfmpegInstallStatus::already_present,
                 "an installed FFmpeg executable is reused");
}

} // namespace

int main()
{
    TestSuite suite;
    test_pinned_packages(suite);
    test_member_extraction(suite);
    test_ffmpeg_install(suite);

    if (suite.failures() != 0) {
        std::cerr << suite.failures() << " FFmpeg runtime test assertion(s) failed\n";
        return 1;
    }

    std::cout << "All FFmpeg runtime tests passed\n";
    return 0;
}
