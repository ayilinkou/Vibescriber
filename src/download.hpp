#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string_view>

namespace vibescriber {

enum class DownloadStatus
{
    downloaded,
    already_present,
};

struct DownloadResult
{
    DownloadStatus status;
    std::uintmax_t bytes;
};

class DownloadError final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] DownloadResult download_verified(
    std::string_view url,
    const std::filesystem::path& destination,
    std::string_view expected_sha256);

} // namespace vibescriber
