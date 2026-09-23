// Uses the NeMo-Speech.cpp v0.1.0 C ABI in a separate process to isolate ggml versions.
#include "wav_reader.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

struct Model;
struct Stream;
struct ModelConfig {
    std::size_t size;
    const char* model_path;
    std::int32_t gpu;
    const char* preset;
    std::int32_t chunk_frames, right_context_frames, left_context_frames;
    std::int32_t fifo_frames, spkcache_frames, update_period_frames;
};
struct Segment { double start_time, end_time; std::int32_t speaker; };
using Status = int;

void progress(int percentage)
{
    const int filled = std::clamp(percentage / 5, 0, 20);
    std::cout << "\rDiarization [" << std::string(static_cast<std::size_t>(filled), '=')
              << std::string(static_cast<std::size_t>(20 - filled), ' ')
              << "] " << percentage << "%   " << std::flush;
    if (percentage == 100) std::cout << '\n';
}

class Library {
public:
    explicit Library(const std::filesystem::path& path) {
#ifdef _WIN32
        handle_ = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
        handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
        if (!handle_) {
#ifdef _WIN32
            throw std::runtime_error("could not load NeMo-Speech.cpp library (Windows error "
                                     + std::to_string(GetLastError()) + ")");
#else
            throw std::runtime_error(std::string("could not load NeMo-Speech.cpp library: ") + dlerror());
#endif
        }
    }
    ~Library() {
#ifdef _WIN32
        if (handle_) FreeLibrary(handle_);
#else
        if (handle_) dlclose(handle_);
#endif
    }
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    template <typename T> T symbol(const char* name) const {
#ifdef _WIN32
        auto* address = GetProcAddress(handle_, name);
#else
        auto* address = dlsym(handle_, name);
#endif
        if (!address) throw std::runtime_error(std::string("missing NeMo symbol: ") + name);
        return reinterpret_cast<T>(address);
    }
private:
#ifdef _WIN32
    HMODULE handle_ = nullptr;
#else
    void* handle_ = nullptr;
#endif
};

struct Api {
    using Create = Status (*)(const ModelConfig*, Model**);
    using Destroy = void (*)(Model*);
    using Open = Status (*)(Model*, Stream**);
    using Push = Status (*)(Stream*, const float*, std::size_t, std::int32_t);
    using Finish = Status (*)(Stream*);
    using Close = void (*)(Stream*);
    using Segments = Status (*)(const Stream*, const void*, Segment*, std::size_t, std::size_t*);
    using FrameCount = std::int64_t (*)(const Stream*);
    using FrameStart = std::int64_t (*)(const Stream*);
    using FrameProbs = Status (*)(const Stream*, float*, std::size_t);
    using Error = const char* (*)();
    explicit Api(const Library& library)
        : create(library.symbol<Create>("nemo_speech_diar_create")),
          destroy(library.symbol<Destroy>("nemo_speech_diar_destroy")),
          open(library.symbol<Open>("nemo_speech_diar_stream_open")),
          push(library.symbol<Push>("nemo_speech_diar_stream_push_f32")),
          finish(library.symbol<Finish>("nemo_speech_diar_stream_finish")),
          close(library.symbol<Close>("nemo_speech_diar_stream_close")),
          segments(library.symbol<Segments>("nemo_speech_diar_segments")),
          frame_count(library.symbol<FrameCount>("nemo_speech_diar_frame_count")),
          frame_start(library.symbol<FrameStart>("nemo_speech_diar_frame_probs_start")),
          frame_probs(library.symbol<FrameProbs>("nemo_speech_diar_frame_probs")),
          error(library.symbol<Error>("nemo_speech_asr_last_error")) {}
    Create create;
    Destroy destroy;
    Open open;
    Push push;
    Finish finish;
    Close close;
    Segments segments;
    FrameCount frame_count;
    FrameStart frame_start;
    FrameProbs frame_probs;
    Error error;
    void check(Status status) const {
        if (status != 0) {
            const char* details = error();
            throw std::runtime_error(details ? details : "Sortformer inference failed");
        }
    }
};

void run(const Api& api, const std::filesystem::path& model_path,
         const std::vector<float>& samples, const std::filesystem::path& output_path,
         int gpu)
{
    Model* model = nullptr;
    Stream* stream = nullptr;
    std::vector<std::array<float, 4>> probabilities;
    try {
        const std::string model_string = model_path.string();
        ModelConfig config{};
        config.size = sizeof(config);
        config.model_path = model_string.c_str();
        config.gpu = gpu;
        config.preset = "offline";
        api.check(api.create(&config, &model));
        api.check(api.open(model, &stream));
        const auto capture_frames = [&] {
            const auto count = api.frame_count(stream);
            const auto start = api.frame_start(stream);
            if (count < start || start < 0
                || static_cast<std::size_t>(start) > probabilities.size()) {
                throw std::runtime_error("Sortformer frame probabilities are incomplete");
            }
            std::vector<float> retained(static_cast<std::size_t>(count - start) * 4U);
            if (!retained.empty()) {
                api.check(api.frame_probs(stream, retained.data(), retained.size()));
            }
            for (auto frame = probabilities.size(); frame < static_cast<std::size_t>(count);
                 ++frame) {
                const auto offset = (frame - static_cast<std::size_t>(start)) * 4U;
                probabilities.push_back({retained[offset], retained[offset + 1U],
                                         retained[offset + 2U], retained[offset + 3U]});
            }
        };
        std::cout << "Diarization backend: " << (gpu < 0 ? "CPU" : "Vulkan GPU") << '\n';
        progress(0);
        constexpr std::size_t chunk_samples = 16'000U * 8U;
        for (std::size_t offset = 0; offset < samples.size(); offset += chunk_samples) {
            const auto count = std::min(chunk_samples, samples.size() - offset);
            api.check(api.push(stream, samples.data() + offset, count, 16'000));
            capture_frames();
            const auto percentage = static_cast<int>(
                90.0 * static_cast<double>(offset + count) / static_cast<double>(samples.size()));
            progress(percentage);
        }
        api.check(api.finish(stream));
        capture_frames();
        std::size_t count = 0;
        api.check(api.segments(stream, nullptr, nullptr, 0, &count));
        std::vector<Segment> segments(count);
        if (count != 0) api.check(api.segments(stream, nullptr, segments.data(), count, &count));
        std::ofstream output(output_path, std::ios::trunc);
        if (!output) throw std::runtime_error("could not open diarization result file");
        output << std::fixed << std::setprecision(4);
        for (std::size_t index = 0; index < count; ++index) {
            output << "S\t" << segments[index].start_time << '\t'
                   << segments[index].end_time << '\t'
                   << segments[index].speaker << '\n';
        }
        output << std::setprecision(6);
        for (std::size_t index = 0; index < probabilities.size(); ++index) {
            const auto& frame = probabilities[index];
            output << "P\t" << index << '\t' << frame[0] << '\t' << frame[1]
                   << '\t' << frame[2] << '\t' << frame[3] << '\n';
        }
        output.close();
        if (!output) throw std::runtime_error("could not write diarization results");
        progress(100);
    } catch (...) {
        if (stream) api.close(stream);
        if (model) api.destroy(model);
        throw;
    }
    api.close(stream);
    api.destroy(model);
}

} // namespace

int main(int argc, char* argv[])
{
    std::cout << std::unitbuf;
    if (argc != 5) {
        std::cerr << "usage: vibescriber_sortformer <library> <model> <wav> <output>\n";
        return 1;
    }
    try {
        Library library(argv[1]);
        Api api(library);
        const auto samples = vibescriber::read_mono_16khz_pcm16_wav(argv[3]);
        if (samples.empty()) throw std::runtime_error("audio is empty");
#if VIBESCRIBER_HAS_VULKAN
        try {
            run(api, argv[2], samples, argv[4], 0);
        } catch (const std::exception& error) {
            std::cerr << "warning: GPU diarization failed: " << error.what()
                      << "; retrying on CPU\n";
            run(api, argv[2], samples, argv[4], -1);
        }
#else
        run(api, argv[2], samples, argv[4], -1);
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: Sortformer diarization failed: " << error.what() << '\n';
        return 1;
    }
}
