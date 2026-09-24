#pragma once

#include <filesystem>
#include <functional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace vibescriber {

class ProcessError final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] int run_process(
    const std::filesystem::path& executable,
    std::span<const std::filesystem::path> arguments);

// The callback receives raw stdout/stderr chunks, including carriage returns.
[[nodiscard]] int run_process_capture(
    const std::filesystem::path& executable,
    std::span<const std::filesystem::path> arguments,
    const std::function<void(std::string_view)>& on_output);

inline int run_process(
    const std::filesystem::path& executable,
    const std::vector<std::filesystem::path>& arguments)
{
    return run_process(executable, std::span<const std::filesystem::path>(arguments));
}

} // namespace vibescriber
