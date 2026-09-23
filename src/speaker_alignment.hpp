#pragma once

#include "transcription.hpp"

#include <filesystem>
#include <vector>

namespace vibescriber {

struct SpeakerInterval
{
    std::int64_t start_centiseconds;
    std::int64_t end_centiseconds;
    int speaker_id;
};

[[nodiscard]] std::vector<SpeakerInterval> read_speaker_intervals(
    const std::filesystem::path& path);
void assign_speakers(std::vector<TranscriptSegment>& words,
                     const std::vector<SpeakerInterval>& intervals);

} // namespace vibescriber
