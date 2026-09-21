#include "cli.hpp"
#include "output_path.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

int main(const int argc, char* argv[])
{
    const std::string_view program_name = argc > 0 ? argv[0] : "vibescriber";
    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    try {
        const vibescriber::CliParseResult parsed = vibescriber::parse_cli(arguments);

        if (parsed.action == vibescriber::CliAction::show_help) {
            std::cout << vibescriber::cli_usage(program_name);
            return EXIT_SUCCESS;
        }
        if (parsed.action == vibescriber::CliAction::show_version) {
            std::cout << "Vibescriber " << VIBESCRIBER_VERSION << '\n';
            return EXIT_SUCCESS;
        }

        if (!std::filesystem::is_regular_file(parsed.options.input_file)) {
            std::cerr << "error: input file does not exist or is not a regular file: "
                      << parsed.options.input_file << '\n';
            return EXIT_FAILURE;
        }

        const std::filesystem::path requested_output =
            parsed.options.output_file.value_or(
                vibescriber::default_output_path(parsed.options.input_file));
        const std::filesystem::path selected_output =
            vibescriber::available_output_path(requested_output, parsed.options.force);

        std::cout << "Input:  " << parsed.options.input_file << '\n'
                  << "Output: " << selected_output << '\n';
        std::cerr << "error: transcription is not implemented yet\n";
        return EXIT_FAILURE;
    } catch (const vibescriber::CliError& error) {
        std::cerr << "error: " << error.what() << "\n\n"
                  << vibescriber::cli_usage(program_name);
        return EXIT_FAILURE;
    } catch (const std::filesystem::filesystem_error& error) {
        std::cerr << "error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
