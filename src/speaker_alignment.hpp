#pragma once

#include "transcription.hpp"

#include <filesystem>
#include <array>
#include <vector>

namespace vibescriber {

struct SpeakerInterval
{
    std::int64_t start_centiseconds;
    std::int64_t end_centiseconds;
    int speaker_id;
};

struct DiarizationResult
{
    std::vector<SpeakerInterval> intervals;
    std::vector<std::array<float, 4>> frame_probabilities;
};

[[nodiscard]] DiarizationResult read_diarization_result(
    const std::filesystem::path& path);
void assign_speakers(std::vector<TranscriptSegment>& words,
                     const std::vector<SpeakerInterval>& intervals);
void assign_speakers(std::vector<TranscriptSegment>& words,
                     const DiarizationResult& result);

} // namespace vibescriber
