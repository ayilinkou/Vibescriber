#include "speaker_alignment.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace vibescriber {

std::vector<SpeakerInterval> read_speaker_intervals(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input) throw std::runtime_error("could not read Sortformer diarization results");
    std::vector<SpeakerInterval> intervals;
    double start = 0;
    double end = 0;
    int speaker = 0;
    while (input >> start >> end >> speaker) {
        if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || end < start
            || speaker < 1 || speaker > 4) {
            throw std::runtime_error("Sortformer returned invalid speaker intervals");
        }
        intervals.push_back({static_cast<std::int64_t>(std::llround(start * 100)),
                             static_cast<std::int64_t>(std::llround(end * 100)), speaker});
    }
    if (!input.eof()) throw std::runtime_error("Sortformer results are malformed");
    return intervals;
}

void assign_speakers(std::vector<TranscriptSegment>& words,
                     const std::vector<SpeakerInterval>& intervals)
{
    for (auto& word : words) {
        std::int64_t best_overlap = 0;
        int best_speaker = 0;
        for (const auto& interval : intervals) {
            const auto overlap = std::max<std::int64_t>(0,
                std::min(word.end_centiseconds, interval.end_centiseconds)
                - std::max(word.start_centiseconds, interval.start_centiseconds));
            if (overlap > best_overlap) {
                best_overlap = overlap;
                best_speaker = interval.speaker_id;
            }
        }
        if (best_speaker == 0) {
            std::int64_t nearest = std::numeric_limits<std::int64_t>::max();
            for (const auto& interval : intervals) {
                const auto distance = std::max<std::int64_t>(0,
                    std::max(interval.start_centiseconds - word.end_centiseconds,
                             word.start_centiseconds - interval.end_centiseconds));
                if (distance < nearest) {
                    nearest = distance;
                    best_speaker = interval.speaker_id;
                }
            }
            if (nearest > 100) best_speaker = 0;
        }
        word.speaker_id = best_speaker;
    }
}

} // namespace vibescriber
