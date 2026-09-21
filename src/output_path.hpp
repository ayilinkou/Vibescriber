#pragma once

#include <filesystem>

namespace vibescriber {

[[nodiscard]] std::filesystem::path default_output_path(
    const std::filesystem::path& input_file);

[[nodiscard]] std::filesystem::path available_output_path(
    const std::filesystem::path& requested_output,
    bool force);

} // namespace vibescriber
