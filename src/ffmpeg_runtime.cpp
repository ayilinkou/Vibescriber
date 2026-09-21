#include "ffmpeg_runtime.hpp"

#include "archive_extract.hpp"

#include <stdexcept>
#include <system_error>

namespace vibescriber {
namespace {

[[nodiscard]] bool is_safe_relative_path(const std::filesystem::path& path)
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

const FfmpegPackage& ffmpeg_package(const OperatingSystem operating_system)
{
    static const FfmpegPackage windows_package{
        .archive = {
            .display_name = "FFmpeg 8.1 Windows x86-64",
            .url = "https://github.com/BtbN/FFmpeg-Builds/releases/download/"
                   "autobuild-2026-09-17-13-19/"
                   "ffmpeg-n8.1.2-54-gc573a95381-win64-lgpl-8.1.zip",
            .sha256 = "c740a325e7eeeddd373764cd274b50768190b4030a726fe938dca93f968dfc10",
            .relative_path = "downloads/ffmpeg-n8.1.2-54-gc573a95381-win64-lgpl-8.1.zip",
            .expected_bytes = 169'508'589U,
        },
        .archive_member =
            "ffmpeg-n8.1.2-54-gc573a95381-win64-lgpl-8.1/bin/ffmpeg.exe",
        .executable_relative_path = "tools/ffmpeg.exe",
        .set_executable_permissions = false,
    };
    static const FfmpegPackage linux_package{
        .archive = {
            .display_name = "FFmpeg 8.1 Linux x86-64",
            .url = "https://github.com/BtbN/FFmpeg-Builds/releases/download/"
                   "autobuild-2026-09-17-13-19/"
                   "ffmpeg-n8.1.2-54-gc573a95381-linux64-lgpl-8.1.tar.xz",
            .sha256 = "3846384ed094486b54d44b05cdc2076a34607295cfdcce612efd6e4db2319249",
            .relative_path = "downloads/ffmpeg-n8.1.2-54-gc573a95381-linux64-lgpl-8.1.tar.xz",
            .expected_bytes = 136'031'360U,
        },
        .archive_member =
            "ffmpeg-n8.1.2-54-gc573a95381-linux64-lgpl-8.1/bin/ffmpeg",
        .executable_relative_path = "tools/ffmpeg",
        .set_executable_permissions = true,
    };

    if (operating_system == OperatingSystem::windows_host) {
        return windows_package;
    }
    return linux_package;
}

FfmpegInstallResult ensure_ffmpeg(
    const std::filesystem::path& data_directory,
    const FfmpegPackage& package)
{
    if (data_directory.empty()) {
        throw std::invalid_argument("the application data directory may not be empty");
    }
    if (!is_safe_relative_path(package.archive_member)
        || !is_safe_relative_path(package.executable_relative_path)) {
        throw std::invalid_argument("FFmpeg package paths must be safe relative paths");
    }

    const auto executable_path = data_directory / package.executable_relative_path;
    if (std::filesystem::is_regular_file(executable_path)) {
        return {FfmpegInstallStatus::already_present, executable_path};
    }

    (void)ensure_runtime_asset(data_directory, package.archive);
    const auto archive_path = runtime_asset_path(data_directory, package.archive);
    extract_archive_member(archive_path, package.archive_member, executable_path);

    if (package.set_executable_permissions) {
        std::filesystem::permissions(
            executable_path,
            std::filesystem::perms::owner_exec
                | std::filesystem::perms::group_exec
                | std::filesystem::perms::others_exec,
            std::filesystem::perm_options::add);
    }

    std::error_code cleanup_error;
    std::filesystem::remove(archive_path, cleanup_error);
    return {FfmpegInstallStatus::installed, executable_path};
}

} // namespace vibescriber
