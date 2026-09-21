#include "transcription.hpp"

#include "wav_reader.hpp"

#include <whisper.h>

#include <limits>
#include <memory>
#include <string>

namespace vibescriber {
namespace {

struct WhisperContextDeleter
{
    void operator()(whisper_context* context) const
    {
        whisper_free(context);
    }
};

} // namespace

std::vector<TranscriptSegment> transcribe_wav(
    const std::filesystem::path& model_path,
    const std::filesystem::path& wav_path)
{
    if (!std::filesystem::is_regular_file(model_path)) {
        throw TranscriptionError("the transcription model does not exist: "
                                 + model_path.string());
    }

    std::vector<float> samples;
    try {
        samples = read_mono_16khz_pcm16_wav(wav_path);
    } catch (const WavError& error) {
        throw TranscriptionError(error.what());
    }
    if (samples.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw TranscriptionError("the converted audio contains too many samples");
    }

    whisper_context_params context_parameters = whisper_context_default_params();
    context_parameters.use_gpu = false;
    const std::string model_path_string = model_path.string();
    std::unique_ptr<whisper_context, WhisperContextDeleter> context(
        whisper_init_from_file_with_params(
            model_path_string.c_str(), context_parameters));
    if (!context) {
        throw TranscriptionError("failed to load the transcription model");
    }

    whisper_full_params parameters = whisper_full_default_params(
        WHISPER_SAMPLING_GREEDY);
    parameters.language = "en";
    parameters.translate = false;
    parameters.tdrz_enable = true;
    parameters.print_progress = false;
    parameters.print_realtime = false;
    parameters.print_timestamps = false;

    if (whisper_full(
            context.get(),
            parameters,
            samples.data(),
            static_cast<int>(samples.size())) != 0) {
        throw TranscriptionError("transcription failed");
    }

    const int count = whisper_full_n_segments(context.get());
    std::vector<TranscriptSegment> segments;
    segments.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        const char* text = whisper_full_get_segment_text(context.get(), index);
        segments.push_back({
            .text = text == nullptr ? std::string{} : text,
            .start_centiseconds = whisper_full_get_segment_t0(context.get(), index),
            .end_centiseconds = whisper_full_get_segment_t1(context.get(), index),
            .speaker_turn_after = whisper_full_get_segment_speaker_turn_next(
                context.get(), index),
        });
    }
    return segments;
}

} // namespace vibescriber
