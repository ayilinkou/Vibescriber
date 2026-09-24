#include "cli.hpp"

#include <sstream>

namespace vibescriber {

CliParseResult parse_cli(const std::span<const std::string_view> arguments)
{
    CliParseResult result;
    bool options_enabled = true;
    bool cpu_profile_specified = false;

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

        if (options_enabled && (argument == "-m" || argument == "--model")) {
            if (result.options.model_file.has_value()) {
                throw CliError("the model option may only be specified once");
            }
            if (++index >= arguments.size() || arguments[index].empty()) {
                throw CliError("the model option requires a path");
            }

            result.options.model_file = std::filesystem::path(arguments[index]);
            continue;
        }

        if (options_enabled && argument == "--timestamps") {
            result.options.timestamps = true;
            continue;
        }

        if (options_enabled && argument == "--tinydiarize") {
            result.options.tinydiarize = true;
            continue;
        }

        if (options_enabled && argument == "--diarize") {
            result.options.diarize = true;
            continue;
        }

        if (options_enabled && argument == "--force") {
            result.options.force = true;
            continue;
        }

        if (options_enabled && (argument == "--slow" || argument == "--fast")) {
            if (cpu_profile_specified) {
                throw CliError("only one CPU usage option may be specified");
            }
            cpu_profile_specified = true;
            result.options.cpu_profile = argument == "--slow"
                                             ? CpuProfile::slow
                                             : CpuProfile::fast;
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
    if (result.options.diarize && result.options.tinydiarize) {
        throw CliError("--diarize and --tinydiarize cannot be combined");
    }

    return result;
}

TranscriptionMode select_transcription_mode(const CliOptions& options)
{
    const bool sortformer = options.diarize
        || (!options.model_file.has_value() && !options.tinydiarize);
    TranscriptionModel model = TranscriptionModel::custom;
    if (!options.model_file.has_value()) {
        model = sortformer ? TranscriptionModel::medium : TranscriptionModel::small;
    } else if (*options.model_file == "medium.en") {
        model = TranscriptionModel::medium;
    } else if (*options.model_file == "small.en-tdrz") {
        model = TranscriptionModel::small;
    }
    if (model == TranscriptionModel::medium && options.tinydiarize) {
        throw CliError("Whisper medium.en does not support TinyDiarize");
    }
    return {model, sortformer,
            !sortformer && (model == TranscriptionModel::small || options.tinydiarize)};
}

std::string cli_usage(const std::string_view program_name)
{
    std::ostringstream output;
    output << "Usage: " << program_name << " [options] <audio-file>\n\n"
           << "Options:\n"
           << "  -o, --output <path>  Choose the output file\n"
           << "  -m, --model <name|path>  Model: small.en-tdrz, medium.en, or a GGML file\n"
           << "      --timestamps    Include timestamps in the transcript\n"
           << "      --tinydiarize   Detect speaker turns with a TinyDiarize model\n"
           << "      --diarize       Identify up to 4 speakers with Sortformer v2\n"
           << "      --slow          Use about one quarter of logical CPU threads\n"
           << "      --fast          Use all logical CPU threads\n"
           << "      --force         Replace the requested output file\n"
           << "  -h, --help          Show this help text\n"
           << "      --version       Show the Vibescriber version\n\n"
           << "Default: medium.en transcription with Sortformer speaker labels.\n";
    return output.str();
}

} // namespace vibescriber
