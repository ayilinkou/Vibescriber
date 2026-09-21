#pragma once

#include "transcription.hpp"

#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>

namespace vibescriber {

class TranscriptOutputError final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::string format_transcript(
    std::span<const TranscriptSegment> segments,
    bool include_timestamps);

void write_transcript(
    const std::filesystem::path& path,
    const std::string& contents);

} // namespace vibescriber
