#include "transcription.hpp"

#include "wav_reader.hpp"

#include <whisper.h>
#if VIBESCRIBER_HAS_VULKAN || VIBESCRIBER_DYNAMIC_BACKENDS
#include <ggml-backend.h>
#endif

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

bool vulkan_gpu_available()
{
#if VIBESCRIBER_HAS_VULKAN
    // Dynamic backends include Vulkan only when its loader and a device work.
    return ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU) != nullptr
           || ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_IGPU) != nullptr;
#else
    return false;
#endif
}

struct ProgressContext
{
    const TranscriptionProgress* callback;
    std::chrono::steady_clock::time_point started_at;
    bool callback_failed = false;
};

void report_progress(
    whisper_context*,
    whisper_state*,
    const int percentage,
    void* user_data)
{
    auto& context = *static_cast<ProgressContext*>(user_data);
    if (percentage >= 100 || context.callback_failed) {
        return;
    }
    try {
        (*context.callback)(
            percentage, std::chrono::steady_clock::now() - context.started_at);
    } catch (...) {
        context.callback_failed = true;
    }
}

void discard_whisper_log(ggml_log_level, const char*, void*)
{
}

} // namespace

std::vector<TranscriptSegment> transcribe_wav(
    const std::filesystem::path& model_path,
    const std::filesystem::path& wav_path,
    const TranscriptionOptions& options)
{
    if (options.thread_count < 1) {
        throw TranscriptionError("the transcription thread count must be positive");
    }
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

#if VIBESCRIBER_DYNAMIC_BACKENDS
    ggml_backend_load_all();
#endif

    whisper_context_params context_parameters = whisper_context_default_params();
    context_parameters.use_gpu = vulkan_gpu_available();
    whisper_log_set(&discard_whisper_log, nullptr);
    const std::string model_path_string = model_path.string();
    auto load_model = [&]() {
        return std::unique_ptr<whisper_context, WhisperContextDeleter>(
            whisper_init_from_file_with_params(
                model_path_string.c_str(), context_parameters));
    };
    auto context = load_model();
    if (!context && context_parameters.use_gpu) {
        context_parameters.use_gpu = false;
        context = load_model();
    }
    if (!context) {
        throw TranscriptionError("failed to load the transcription model");
    }

    whisper_full_params parameters = whisper_full_default_params(
        WHISPER_SAMPLING_GREEDY);
    parameters.language = "en";
    parameters.n_threads = options.thread_count;
    parameters.translate = false;
    parameters.tdrz_enable = true;
    parameters.print_progress = false;
    parameters.print_realtime = false;
    parameters.print_timestamps = false;
    ProgressContext progress_context{&options.progress, {}};
    if (options.progress) {
        parameters.progress_callback = &report_progress;
        parameters.progress_callback_user_data = &progress_context;
    }

    progress_context.started_at = std::chrono::steady_clock::now();
    if (options.progress) {
        options.progress(0, std::chrono::steady_clock::duration::zero());
    }
    int transcription_result = whisper_full(
            context.get(),
            parameters,
            samples.data(),
            static_cast<int>(samples.size()));
    if (transcription_result != 0 && context_parameters.use_gpu) {
        context_parameters.use_gpu = false;
        context = load_model();
        if (!context) {
            throw TranscriptionError("failed to load the transcription model on the CPU");
        }
        transcription_result = whisper_full(
            context.get(), parameters, samples.data(), static_cast<int>(samples.size()));
    }
    if (progress_context.callback_failed) {
        throw TranscriptionError("the transcription progress callback failed");
    }
    if (transcription_result != 0) {
        throw TranscriptionError("transcription failed");
    }
    if (options.progress) {
        options.progress(
            100, std::chrono::steady_clock::now() - progress_context.started_at);
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
