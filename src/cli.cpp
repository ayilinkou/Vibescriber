#include "cli.hpp"

#include <sstream>

namespace vibescriber {

CliParseResult parse_cli(const std::span<const std::string_view> arguments)
{
    CliParseResult result;
    bool options_enabled = true;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];

        if (options_enabled && argument == "--") {
            options_enabled = false;
            continue;
        }

        if (options_enabled && (argument == "-h" || argument == "--help")) {
            result.action = CliAction::show_help;
            return result;
        }

        if (options_enabled && argument == "--version") {
            result.action = CliAction::show_version;
            return result;
        }

        if (options_enabled && (argument == "-o" || argument == "--output")) {
            if (result.options.output_file.has_value()) {
                throw CliError("the output option may only be specified once");
            }
            if (++index >= arguments.size() || arguments[index].empty()) {
                throw CliError("the output option requires a path");
            }

            result.options.output_file = std::filesystem::path(arguments[index]);
            continue;
        }

        if (options_enabled && argument == "--timestamps") {
            result.options.timestamps = true;
            continue;
        }

        if (options_enabled && argument == "--force") {
            result.options.force = true;
            continue;
        }

        if (options_enabled && argument.starts_with('-') && argument != "-") {
            throw CliError("unknown option: " + std::string(argument));
        }

        if (!result.options.input_file.empty()) {
            throw CliError("only one input file may be specified");
        }
        if (argument.empty()) {
            throw CliError("the input path may not be empty");
        }

        result.options.input_file = std::filesystem::path(argument);
    }

    if (result.options.input_file.empty()) {
        throw CliError("an input file is required");
    }

    return result;
}

std::string cli_usage(const std::string_view program_name)
{
    std::ostringstream output;
    output << "Usage: " << program_name << " [options] <audio-file>\n\n"
           << "Options:\n"
           << "  -o, --output <path>  Choose the output file\n"
           << "      --timestamps    Include timestamps in the transcript\n"
           << "      --force         Replace the requested output file\n"
           << "  -h, --help          Show this help text\n"
           << "      --version       Show the Vibescriber version\n";
    return output.str();
}

} // namespace vibescriber
