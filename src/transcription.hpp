#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
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

using TranscriptionProgress = std::function<void(
    int percentage, std::chrono::steady_clock::duration elapsed)>;

struct TranscriptionOptions
{
    int thread_count = 4;
    std::function<void(std::string_view)> backend_selected;
    TranscriptionProgress progress;
};

[[nodiscard]] std::vector<TranscriptSegment> transcribe_wav(
    const std::filesystem::path& model_path,
    const std::filesystem::path& wav_path,
    const TranscriptionOptions& options = {});

} // namespace vibescriber
