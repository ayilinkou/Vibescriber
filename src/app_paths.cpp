#include "app_paths.hpp"

#include <cstdlib>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace vibescriber {
namespace {

#ifdef _WIN32
std::optional<std::filesystem::path> environment_path(const wchar_t* name)
{
    const DWORD required_size = GetEnvironmentVariableW(name, nullptr, 0);
    if (required_size == 0) {
        return std::nullopt;
    }

    std::wstring value(required_size, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), required_size);
    if (written == 0 || written >= required_size) {
        throw std::runtime_error("failed to read a Windows environment variable");
    }
    value.resize(written);
    return std::filesystem::path(value);
}
#else
std::optional<std::filesystem::path> environment_path(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::filesystem::path(value);
}
#endif

} // namespace

std::filesystem::path resolve_application_data_directory(
    const OperatingSystem operating_system,
    const DataDirectoryEnvironment& environment)
{
    if (operating_system == OperatingSystem::windows_host) {
        if (!environment.local_app_data.has_value()
            || environment.local_app_data->empty()) {
            throw std::runtime_error("LOCALAPPDATA is not available");
        }
        return *environment.local_app_data / "Vibescriber";
    }

    if (environment.xdg_data_home.has_value()
        && environment.xdg_data_home->is_absolute()) {
        return *environment.xdg_data_home / "vibescriber";
    }
    if (!environment.home.has_value() || !environment.home->is_absolute()) {
        throw std::runtime_error(
            "neither an absolute XDG_DATA_HOME nor HOME is available");
    }
    return *environment.home / ".local" / "share" / "vibescriber";
}

std::filesystem::path application_data_directory()
{
#ifdef _WIN32
    return resolve_application_data_directory(
        OperatingSystem::windows_host,
        {
            .local_app_data = environment_path(L"LOCALAPPDATA"),
            .xdg_data_home = std::nullopt,
            .home = std::nullopt,
        });
#elif defined(__linux__)
    return resolve_application_data_directory(
        OperatingSystem::linux_host,
        {
            .local_app_data = std::nullopt,
            .xdg_data_home = environment_path("XDG_DATA_HOME"),
            .home = environment_path("HOME"),
        });
#else
#error "Vibescriber currently supports only Windows and Linux"
#endif
}

} // namespace vibescriber
