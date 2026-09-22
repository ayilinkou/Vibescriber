#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace vibescriber {

struct TranscriptSegment
{
    std::string text;
    std::int64_t start_centiseconds;
    std::int64_t end_centiseconds;
    bool speaker_turn_after;
};

class TranscriptionError final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

using TranscriptionProgress = std::function<void(int percentage)>;

struct TranscriptionOptions
{
    int thread_count = 4;
    TranscriptionProgress progress;
};

[[nodiscard]] std::vector<TranscriptSegment> transcribe_wav(
    const std::filesystem::path& model_path,
    const std::filesystem::path& wav_path,
    const TranscriptionOptions& options = {});

} // namespace vibescriber
