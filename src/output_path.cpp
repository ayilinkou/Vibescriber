#include "output_path.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace vibescriber {

std::filesystem::path default_output_path(const std::filesystem::path& input_file)
{
    if (input_file.empty()) {
        throw std::invalid_argument("the input path may not be empty");
    }

    std::filesystem::path output = input_file;
    output.replace_extension(".txt");
    return output;
}

std::filesystem::path available_output_path(
    const std::filesystem::path& requested_output,
    const bool force)
{
    if (requested_output.empty()) {
        throw std::invalid_argument("the output path may not be empty");
    }

    if (force || !std::filesystem::exists(requested_output)) {
        return requested_output;
    }

    for (std::size_t suffix = 1;; ++suffix) {
        std::filesystem::path filename = requested_output.stem();
        filename += "_";
        filename += std::to_string(suffix);
        filename += requested_output.extension();

        std::filesystem::path candidate = requested_output.parent_path() / filename;
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
}

} // namespace vibescriber
