#include "download.hpp"

#include "sha256.hpp"

#include <curl/curl.h>

#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

namespace vibescriber {
namespace {

class CurlGlobal
{
public:
    CurlGlobal()
    {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            throw DownloadError("failed to initialize libcurl");
        }
    }

    ~CurlGlobal()
    {
        curl_global_cleanup();
    }
};

struct CurlDeleter
{
    void operator()(CURL* handle) const
    {
        curl_easy_cleanup(handle);
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

struct WriteContext
{
    std::ofstream* output;
    std::uintmax_t bytes = 0;
};

struct ProgressContext
{
    const DownloadProgress* callback;
    bool callback_failed = false;
};

std::size_t write_download(
    char* data,
    const std::size_t size,
    const std::size_t count,
    void* user_data)
{
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
        return 0;
    }

    const std::size_t byte_count = size * count;
    auto& context = *static_cast<WriteContext*>(user_data);
    context.output->write(data, static_cast<std::streamsize>(byte_count));
    if (!*context.output) {
        return 0;
    }

    context.bytes += byte_count;
    return byte_count;
}

int report_progress(
    void* user_data,
    const curl_off_t download_total,
    const curl_off_t downloaded,
    curl_off_t,
    curl_off_t)
{
    auto& context = *static_cast<ProgressContext*>(user_data);
    try {
        (*context.callback)(
            downloaded < 0 ? 0U : static_cast<std::uintmax_t>(downloaded),
            download_total < 0 ? 0U : static_cast<std::uintmax_t>(download_total));
        return 0;
    } catch (...) {
        context.callback_failed = true;
        return 1;
    }
}

std::string normalize_digest(const std::string_view digest)
{
    if (digest.size() != 64U) {
        throw DownloadError("an expected SHA-256 digest must contain 64 hexadecimal characters");
    }

    std::string normalized;
    normalized.reserve(digest.size());
    for (const char character : digest) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (std::isxdigit(value) == 0) {
            throw DownloadError("an expected SHA-256 digest contains a non-hexadecimal character");
        }
        normalized.push_back(static_cast<char>(std::tolower(value)));
    }
    return normalized;
}

template <typename Value>
void set_option(CURL* handle, const CURLoption option, Value value)
{
    const CURLcode result = curl_easy_setopt(handle, option, value);
    if (result != CURLE_OK) {
        throw DownloadError(std::string("failed to configure libcurl: ")
                            + curl_easy_strerror(result));
    }
}

} // namespace

DownloadResult download_verified(
    const std::string_view url,
    const std::filesystem::path& destination,
    const std::string_view expected_sha256,
    const DownloadProgress& progress)
{
    if (url.empty()) {
        throw DownloadError("the download URL may not be empty");
    }
    if (destination.empty()) {
        throw DownloadError("the download destination may not be empty");
    }

    const std::string expected_digest = normalize_digest(expected_sha256);
    if (std::filesystem::is_regular_file(destination)
        && sha256_file(destination) == expected_digest) {
        return {DownloadStatus::already_present, std::filesystem::file_size(destination)};
    }

    const std::filesystem::path parent = destination.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    std::filesystem::path staging_path = destination;
    staging_path += ".part";
    PartialFile partial_file(staging_path);

    std::ofstream output(staging_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw DownloadError("could not open the temporary download file: "
                            + staging_path.string());
    }

    static const CurlGlobal curl_global;
    std::unique_ptr<CURL, CurlDeleter> handle(curl_easy_init());
    if (!handle) {
        throw DownloadError("failed to create a libcurl transfer");
    }

    std::array<char, CURL_ERROR_SIZE> error_buffer{};
    WriteContext write_context{&output};
    ProgressContext progress_context{&progress};
    const std::string url_string(url);

    set_option(handle.get(), CURLOPT_URL, url_string.c_str());
    set_option(handle.get(), CURLOPT_ERRORBUFFER, error_buffer.data());
    set_option(handle.get(), CURLOPT_WRITEFUNCTION, &write_download);
    set_option(handle.get(), CURLOPT_WRITEDATA, &write_context);
    set_option(handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
    set_option(handle.get(), CURLOPT_MAXREDIRS, 5L);
    set_option(handle.get(), CURLOPT_FAILONERROR, 1L);
    set_option(handle.get(), CURLOPT_CONNECTTIMEOUT, 30L);
    set_option(handle.get(), CURLOPT_LOW_SPEED_LIMIT, 1L);
    set_option(handle.get(), CURLOPT_LOW_SPEED_TIME, 60L);
    set_option(handle.get(), CURLOPT_NOSIGNAL, 1L);
    set_option(handle.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    set_option(handle.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    set_option(handle.get(), CURLOPT_PROTOCOLS_STR, "https,file");
    set_option(handle.get(), CURLOPT_REDIR_PROTOCOLS_STR, "https");
    set_option(handle.get(), CURLOPT_USERAGENT, "Vibescriber");
    if (progress) {
        set_option(handle.get(), CURLOPT_NOPROGRESS, 0L);
        set_option(handle.get(), CURLOPT_XFERINFOFUNCTION, &report_progress);
        set_option(handle.get(), CURLOPT_XFERINFODATA, &progress_context);
    }

    const CURLcode transfer_result = curl_easy_perform(handle.get());
    output.close();
    if (transfer_result != CURLE_OK) {
        if (progress_context.callback_failed) {
            throw DownloadError("the download progress callback failed");
        }
        const std::string details = error_buffer[0] != '\0'
                                        ? error_buffer.data()
                                        : curl_easy_strerror(transfer_result);
        throw DownloadError("download failed: " + details);
    }
    if (!output) {
        throw DownloadError("failed to finish writing the temporary download file");
    }

    const std::string actual_digest = sha256_file(staging_path);
    if (actual_digest != expected_digest) {
        throw DownloadError("downloaded file failed SHA-256 verification");
    }

    std::error_code error;
    if (std::filesystem::exists(destination, error)) {
        error.clear();
        if (!std::filesystem::remove(destination, error) || error) {
            throw DownloadError("could not replace the existing destination: "
                                + destination.string());
        }
    } else if (error) {
        throw DownloadError("could not inspect the download destination: "
                            + destination.string());
    }

    std::filesystem::rename(staging_path, destination, error);
    if (error) {
        throw DownloadError("could not move the verified download into place: "
                            + destination.string());
    }

    partial_file.commit();
    return {DownloadStatus::downloaded, write_context.bytes};
}

} // namespace vibescriber
