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
    CpuProfile cpu_profile = CpuProfile::balanced;
    bool timestamps = false;
    bool force = false;
};

struct CliParseResult
{
    CliAction action = CliAction::run;
    CliOptions options;
};

class CliError final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] CliParseResult parse_cli(std::span<const std::string_view> arguments);
[[nodiscard]] std::string cli_usage(std::string_view program_name);

} // namespace vibescriber
