#pragma once

#include <filesystem>
#include <span>
#include <stdexcept>
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

inline int run_process(
    const std::filesystem::path& executable,
    const std::vector<std::filesystem::path>& arguments)
{
    return run_process(executable, std::span<const std::filesystem::path>(arguments));
}

} // namespace vibescriber
