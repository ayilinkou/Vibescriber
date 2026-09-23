#include "speaker_alignment.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace vibescriber {

DiarizationResult read_diarization_result(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input) throw std::runtime_error("could not read Sortformer diarization results");
    DiarizationResult result;
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream record(line);
        char kind = '\0';
        record >> kind;
        if (kind == 'S') {
            double start = 0;
            double end = 0;
            int speaker = 0;
            if (!(record >> start >> end >> speaker) || !std::isfinite(start)
                || !std::isfinite(end) || start < 0 || end < start
                || speaker < 1 || speaker > 4) {
                throw std::runtime_error("Sortformer returned invalid speaker intervals");
            }
            result.intervals.push_back({static_cast<std::int64_t>(std::llround(start * 100)),
                                        static_cast<std::int64_t>(std::llround(end * 100)),
                                        speaker});
        } else if (kind == 'P') {
            std::size_t index = 0;
            std::array<float, 4> probabilities{};
            if (!(record >> index >> probabilities[0] >> probabilities[1]
                  >> probabilities[2] >> probabilities[3])
                || index != result.frame_probabilities.size()) {
                throw std::runtime_error("Sortformer returned invalid frame probabilities");
            }
            for (float probability : probabilities) {
                if (!std::isfinite(probability) || probability < 0.0F
                    || probability > 1.0F) {
                    throw std::runtime_error("Sortformer returned invalid frame probabilities");
                }
            }
            result.frame_probabilities.push_back(probabilities);
        } else {
            throw std::runtime_error("Sortformer results are malformed");
        }
        std::string extra;
        if (record >> extra) throw std::runtime_error("Sortformer results are malformed");
    }
    if (!input.eof()) throw std::runtime_error("Sortformer results are malformed");
    return result;
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

void assign_speakers(std::vector<TranscriptSegment>& words,
                     const DiarizationResult& result)
{
    assign_speakers(words, result.intervals);
    const auto& frames = result.frame_probabilities;
    if (frames.empty()) return;

    for (auto& word : words) {
        const auto start = std::max<std::int64_t>(0, word.start_centiseconds);
        const auto duration = std::max<std::int64_t>(0,
            word.end_centiseconds - word.start_centiseconds);
        const auto first_time = start + duration * 35 / 100;
        const auto last_time = start + duration * 95 / 100;
        const auto first_frame = static_cast<std::size_t>(first_time / 8);
        if (first_frame >= frames.size()) continue;
        const auto last_frame = std::min<std::size_t>(frames.size() - 1U,
            static_cast<std::size_t>(last_time / 8));
        std::array<double, 4> scores{};
        for (auto frame = first_frame; frame <= last_frame; ++frame) {
            for (std::size_t speaker = 0; speaker < scores.size(); ++speaker) {
                scores[speaker] += frames[frame][speaker];
            }
        }
        const auto best = std::max_element(scores.begin(), scores.end());
        const auto sample_count = static_cast<double>(last_frame - first_frame + 1U);
        if (*best / sample_count >= 0.5) {
            word.speaker_id = static_cast<int>(best - scores.begin()) + 1;
        }
    }
}

} // namespace vibescriber
