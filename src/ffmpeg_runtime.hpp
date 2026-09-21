#pragma once

#include "app_paths.hpp"
#include "runtime_asset.hpp"

#include <filesystem>

namespace vibescriber {

struct FfmpegPackage
{
    RuntimeAsset archive;
    std::filesystem::path archive_member;
    std::filesystem::path executable_relative_path;
    bool set_executable_permissions;
};

enum class FfmpegInstallStatus
{
    installed,
    already_present,
};

struct FfmpegInstallResult
{
    FfmpegInstallStatus status;
    std::filesystem::path executable_path;
};

[[nodiscard]] const FfmpegPackage& ffmpeg_package(OperatingSystem operating_system);

[[nodiscard]] FfmpegInstallResult ensure_ffmpeg(
    const std::filesystem::path& data_directory,
    const FfmpegPackage& package,
    const DownloadProgress& progress = {});

} // namespace vibescriber
