#pragma once

#include "cpu_profile.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace vibescriber {

enum class CliAction
{
    run,
    show_help,
    show_version,
};

struct CliOptions
{
    std::filesystem::path input_file;
    std::optional<std::filesystem::path> output_file;
    std::optional<std::filesystem::path> model_file;
    CpuProfile cpu_profile = CpuProfile::balanced;
    bool timestamps = false;
    bool tinydiarize = false;
    bool diarize = false;
    bool force = false;
};

struct CliParseResult
{
    CliAction action = CliAction::run;
    CliOptions options;
};

enum class TranscriptionModel { small, medium, custom };

struct TranscriptionMode
{
    TranscriptionModel model;
    bool sortformer;
    bool tinydiarize;
};

class CliError final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] CliParseResult parse_cli(std::span<const std::string_view> arguments);
[[nodiscard]] TranscriptionMode select_transcription_mode(const CliOptions& options);
[[nodiscard]] std::string cli_usage(std::string_view program_name);

} // namespace vibescriber
