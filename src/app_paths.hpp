#pragma once

#include <filesystem>
#include <optional>

namespace vibescriber {

enum class OperatingSystem
{
    windows_host,
    linux_host,
};

struct DataDirectoryEnvironment
{
    std::optional<std::filesystem::path> local_app_data;
    std::optional<std::filesystem::path> xdg_data_home;
    std::optional<std::filesystem::path> home;
};

[[nodiscard]] std::filesystem::path resolve_application_data_directory(
    OperatingSystem operating_system,
    const DataDirectoryEnvironment& environment);

[[nodiscard]] OperatingSystem current_operating_system();
[[nodiscard]] std::filesystem::path application_data_directory();

} // namespace vibescriber
