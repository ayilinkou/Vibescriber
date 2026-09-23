#include "app_paths.hpp"
#include "audio_conversion.hpp"
#include "cli.hpp"
#include "ffmpeg_runtime.hpp"
#include "output_path.hpp"
#include "runtime_asset.hpp"
#include "sortformer_runtime.hpp"
#include "speaker_alignment.hpp"
#include "process.hpp"
#include "transcript_output.hpp"
#include "transcription.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

class TemporaryWav
{
public:
    explicit TemporaryWav(const std::filesystem::path& data_directory)
    {
        const auto cache_directory = data_directory / "cache";
        std::filesystem::create_directories(cache_directory);

        std::random_device random;
        const auto clock_value = std::chrono::steady_clock::now()
                                     .time_since_epoch()
                                     .count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            const std::uint64_t random_value =
                (static_cast<std::uint64_t>(random()) << 32U)
                ^ static_cast<std::uint64_t>(random());
            path_ = cache_directory
                    / ("audio-" + std::to_string(clock_value) + '-'
                       + std::to_string(random_value) + ".wav");
            if (!std::filesystem::exists(path_)) {
                return;
            }
        }
        throw std::runtime_error("could not allocate a temporary audio filename");
    }

    ~TemporaryWav()
    {
        std::error_code error;
        std::filesystem::remove(path_, error);
        std::filesystem::path partial = path_;
        partial += ".part";
        std::filesystem::remove(partial, error);
        std::filesystem::path speakers = path_;
        speakers += ".speakers";
        std::filesystem::remove(speakers, error);
    }

    TemporaryWav(const TemporaryWav&) = delete;
    TemporaryWav& operator=(const TemporaryWav&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

std::filesystem::path companion_executable()
{
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        throw std::runtime_error("could not locate the Vibescriber executable");
    }
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path() / "vibescriber_sortformer.exe";
#else
    return std::filesystem::read_symlink("/proc/self/exe").parent_path()
           / "vibescriber_sortformer";
#endif
}

class DownloadProgressPrinter
{
public:
    explicit DownloadProgressPrinter(std::string label)
        : label_(std::move(label))
    {
    }

    void operator()(const std::uintmax_t downloaded, const std::uintmax_t total)
    {
        if (total != 0U) {
            const int percentage = static_cast<int>(
                static_cast<long double>(downloaded) * 100.0L
                / static_cast<long double>(total));
            const int bucket = percentage / 10;
            if (bucket > last_bucket_) {
                last_bucket_ = bucket;
                std::cout << "  " << label_ << ": " << percentage << "% ("
                          << std::fixed << std::setprecision(1)
                          << static_cast<long double>(downloaded) / 1'048'576.0L
                          << " MiB)\n";
            }
            return;
        }

        const std::uintmax_t bucket = downloaded / (50U * 1'048'576U);
        if (bucket > unknown_size_bucket_) {
            unknown_size_bucket_ = bucket;
            std::cout << "  " << label_ << ": " << std::fixed
                      << std::setprecision(1)
                      << static_cast<long double>(downloaded) / 1'048'576.0L
                      << " MiB\n";
        }
    }

private:
    std::string label_;
    int last_bucket_ = -1;
    std::uintmax_t unknown_size_bucket_ = 0;
};

class TranscriptionProgressPrinter
{
public:
    ~TranscriptionProgressPrinter()
    {
        stop();
    }

    void reset()
    {
        stop();
        std::lock_guard lock(mutex_);
        started_ = false;
        done_ = false;
        last_percentage_ = -1;
    }

    void operator()(
        const int percentage,
        const std::chrono::steady_clock::duration elapsed)
    {
        std::unique_lock lock(mutex_);
        if (!started_) {
            started_ = true;
            started_at_ = std::chrono::steady_clock::now() - elapsed;
            ticker_ = std::thread([this] { tick(); });
        }
        if (percentage != last_percentage_ || percentage == 100) {
            last_percentage_ = percentage;
            render(elapsed);
        }
        if (percentage == 100) {
            done_ = true;
            condition_.notify_all();
            std::cout << '\n';
            lock.unlock();
            ticker_.join();
        }
    }

private:
    void render(const std::chrono::steady_clock::duration elapsed) const
    {
        const auto total_seconds =
            std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        const auto hours = total_seconds / 3'600;
        const auto minutes = total_seconds / 60 % 60;
        const auto seconds = total_seconds % 60;
        std::ostringstream line;
        line << '[' << std::setfill('0') << std::setw(2) << hours << ':'
             << std::setw(2) << minutes << ':' << std::setw(2) << seconds
             << "] " << last_percentage_ << '%';
        std::cout << '\r' << line.str() << "   " << std::flush;
    }

    void tick()
    {
        std::unique_lock lock(mutex_);
        while (!done_) {
            if (condition_.wait_for(
                    lock, std::chrono::seconds(1), [this] { return done_; })) {
                break;
            }
            render(std::chrono::steady_clock::now() - started_at_);
        }
    }

    void stop()
    {
        {
            std::lock_guard lock(mutex_);
            if (started_ && !done_) {
                done_ = true;
                std::cout << '\n';
            }
        }
        condition_.notify_all();
        if (ticker_.joinable()) {
            ticker_.join();
        }
    }

    std::mutex mutex_;
    std::condition_variable condition_;
    std::chrono::steady_clock::time_point started_at_{};
    std::thread ticker_;
    bool started_ = false;
    bool done_ = false;
    int last_percentage_ = -1;
};

} // namespace

int main(const int argc, char* argv[])
{
    std::cout << std::unitbuf;
    const std::string_view program_name = argc > 0 ? argv[0] : "vibescriber";
    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    try {
        const vibescriber::CliParseResult parsed = vibescriber::parse_cli(arguments);

        if (parsed.action == vibescriber::CliAction::show_help) {
            std::cout << vibescriber::cli_usage(program_name);
            return EXIT_SUCCESS;
        }
        if (parsed.action == vibescriber::CliAction::show_version) {
            std::cout << "Vibescriber " << VIBESCRIBER_VERSION << '\n';
            return EXIT_SUCCESS;
        }

        if (!std::filesystem::is_regular_file(parsed.options.input_file)) {
            std::cerr << "error: input file does not exist or is not a regular file: "
                      << parsed.options.input_file << '\n';
            return EXIT_FAILURE;
        }

        const std::filesystem::path requested_output =
            parsed.options.output_file.value_or(
                vibescriber::default_output_path(parsed.options.input_file));
        const std::filesystem::path selected_output =
            vibescriber::available_output_path(requested_output, parsed.options.force);
        if (std::filesystem::exists(selected_output)
            && std::filesystem::equivalent(parsed.options.input_file, selected_output)) {
            std::cerr << "error: the transcript output may not replace the input file\n";
            return EXIT_FAILURE;
        }

        const bool using_medium_model = (parsed.options.diarize
            && !parsed.options.model_file.has_value())
            || (parsed.options.model_file.has_value()
                && *parsed.options.model_file == "medium.en");
        const bool using_default_model = (!using_medium_model
            && !parsed.options.model_file.has_value())
            || (parsed.options.model_file.has_value()
                && *parsed.options.model_file == "small.en-tdrz");
        const bool using_custom_model = !using_default_model && !using_medium_model;
        if (using_medium_model && parsed.options.tinydiarize) {
            std::cerr << "error: Whisper medium.en does not support TinyDiarize\n";
            return EXIT_FAILURE;
        }
        if (using_custom_model
            && !std::filesystem::is_regular_file(*parsed.options.model_file)) {
            std::cerr << "error: transcription model does not exist or is not a regular file: "
                      << *parsed.options.model_file << '\n';
            return EXIT_FAILURE;
        }
        if (using_custom_model && std::filesystem::exists(selected_output)
            && std::filesystem::equivalent(*parsed.options.model_file, selected_output)) {
            std::cerr << "error: the transcript output may not replace the transcription model\n";
            return EXIT_FAILURE;
        }

        const auto& default_model = vibescriber::transcription_model_asset();
        const auto& medium_model = vibescriber::medium_transcription_model_asset();
        const auto& selected_builtin_model = using_medium_model ? medium_model
                                                                 : default_model;
        const bool enable_tinydiarize = !parsed.options.diarize
            && (using_default_model || parsed.options.tinydiarize);

        std::cout << "Input:  " << parsed.options.input_file << '\n'
                  << "Output: " << selected_output << '\n';
        if (using_custom_model) {
            std::cout << "Model:  " << *parsed.options.model_file << '\n';
        } else {
            std::cout << "Model:  " << selected_builtin_model.display_name << '\n';
        }
        std::cout << "Speaker turns: "
                  << (parsed.options.diarize ? "Sortformer v2"
                      : enable_tinydiarize ? "TinyDiarize" : "off") << '\n';

        const std::filesystem::path data_directory =
            vibescriber::application_data_directory();
        const auto& ffmpeg_package = vibescriber::ffmpeg_package(
            vibescriber::current_operating_system());
        std::cout << "Preparing FFmpeg...\n";
        DownloadProgressPrinter ffmpeg_progress(ffmpeg_package.archive.display_name);
        const auto ffmpeg = vibescriber::ensure_ffmpeg(
            data_directory,
            ffmpeg_package,
            [&ffmpeg_progress](const std::uintmax_t downloaded,
                               const std::uintmax_t total) {
                ffmpeg_progress(downloaded, total);
            });
        std::cout << "  FFmpeg ready.\n";

        const std::filesystem::path model_path = using_custom_model
            ? *parsed.options.model_file
            : vibescriber::runtime_asset_path(data_directory, selected_builtin_model);
        if (!using_custom_model) {
            const auto prepare_model = [&data_directory](
                                           const vibescriber::RuntimeAsset& model) {
                DownloadProgressPrinter progress(model.display_name);
                (void)vibescriber::ensure_runtime_asset(
                    data_directory,
                    model,
                    [&progress](const std::uintmax_t downloaded,
                                const std::uintmax_t total) {
                        progress(downloaded, total);
                    });
            };
            std::cout << "Preparing transcription model...\n";
            prepare_model(selected_builtin_model);
            std::cout << "  Transcription model ready.\n";

            if (!parsed.options.diarize) {
                const auto& comparison_model = using_medium_model ? default_model
                                                                   : medium_model;
                std::cout << "Preparing comparison model...\n";
                try {
                    prepare_model(comparison_model);
                    std::cout << "  Comparison model ready.\n";
                } catch (const std::exception& error) {
                    std::cerr << "warning: could not prepare comparison model: "
                              << error.what() << '\n';
                }
            }
        }

        std::filesystem::path sortformer_library;
        std::filesystem::path sortformer_model;
        if (parsed.options.diarize) {
            std::cout << "Preparing Sortformer v2...\n";
            DownloadProgressPrinter runtime_progress("NeMo-Speech.cpp runtime");
            sortformer_library = vibescriber::ensure_sortformer_library(
                data_directory,
                [&runtime_progress](std::uintmax_t done, std::uintmax_t total) {
                    runtime_progress(done, total);
                });
            const auto& asset = vibescriber::sortformer_model_asset();
            DownloadProgressPrinter model_progress(asset.display_name);
            (void)vibescriber::ensure_runtime_asset(
                data_directory, asset,
                [&model_progress](std::uintmax_t done, std::uintmax_t total) {
                    model_progress(done, total);
                });
            sortformer_model = vibescriber::runtime_asset_path(data_directory, asset);
            std::cout << "  Sortformer v2 ready.\n";
        }

        TemporaryWav converted_audio(data_directory);
        std::cout << "Converting audio...\n";
        vibescriber::convert_audio_to_wav(
            ffmpeg.executable_path,
            parsed.options.input_file,
            converted_audio.path());

        std::cout << "Transcribing locally...\n";
        TranscriptionProgressPrinter transcription_progress;
        const int thread_count = vibescriber::transcription_thread_count(
            parsed.options.cpu_profile,
            std::thread::hardware_concurrency());
        std::cout << "  Using " << thread_count << " worker thread"
                  << (thread_count == 1 ? ".\n" : "s.\n");
        auto segments = vibescriber::transcribe_wav(
            model_path,
            converted_audio.path(),
            {
                .thread_count = thread_count,
                .tinydiarize = enable_tinydiarize,
                .word_timestamps = parsed.options.diarize,
                .backend_selected = [&transcription_progress](
                                        const std::string_view backend) {
                    transcription_progress.reset();
                    std::cout << "Backend: " << backend << '\n';
                },
                .progress = [&transcription_progress](
                                const int percentage,
                                const std::chrono::steady_clock::duration elapsed) {
                    transcription_progress(percentage, elapsed);
                },
            });
        if (parsed.options.diarize) {
            std::cout << "Diarizing locally...\n";
            auto result_path = converted_audio.path();
            result_path += ".speakers";
            const std::vector<std::filesystem::path> arguments{
                sortformer_library, sortformer_model, converted_audio.path(), result_path};
            const int exit_code = vibescriber::run_process(companion_executable(), arguments);
            if (exit_code != 0) {
                throw std::runtime_error("Sortformer diarization failed (exit code "
                                         + std::to_string(exit_code) + ")");
            }
            vibescriber::assign_speakers(
                segments, vibescriber::read_speaker_intervals(result_path));
        }
        const std::string transcript = vibescriber::format_transcript(
            segments,
            parsed.options.timestamps);
        vibescriber::write_transcript(selected_output, transcript);

        std::cout << "Transcript written to " << selected_output << '\n';
        return EXIT_SUCCESS;
    } catch (const vibescriber::CliError& error) {
        std::cerr << "error: " << error.what() << "\n\n"
                  << vibescriber::cli_usage(program_name);
        return EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
