#include "audio_conversion.hpp"

#include "process.hpp"

#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace vibescriber {
namespace {

class PartialFile
{
public:
    explicit PartialFile(std::filesystem::path path)
        : path_(std::move(path))
    {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    ~PartialFile()
    {
        if (!committed_) {
            std::error_code error;
            std::filesystem::remove(path_, error);
        }
    }

    PartialFile(const PartialFile&) = delete;
    PartialFile& operator=(const PartialFile&) = delete;

    void commit()
    {
        committed_ = true;
    }

private:
    std::filesystem::path path_;
    bool committed_ = false;
};

} // namespace

void convert_audio_to_wav(
    const std::filesystem::path& ffmpeg_executable,
    const std::filesystem::path& input_file,
    const std::filesystem::path& output_file)
{
    if (!std::filesystem::is_regular_file(ffmpeg_executable)) {
        throw AudioConversionError("the FFmpeg executable does not exist: "
                                   + ffmpeg_executable.string());
    }
    if (!std::filesystem::is_regular_file(input_file)) {
        throw AudioConversionError("the audio input does not exist: "
                                   + input_file.string());
    }
    if (output_file.empty()) {
        throw AudioConversionError("the audio output path may not be empty");
    }

    const auto parent = output_file.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    std::filesystem::path staging_path = output_file;
    staging_path += ".part";
    PartialFile partial_file(staging_path);

    const std::vector<std::filesystem::path> arguments{
        "-nostdin",
        "-hide_banner",
        "-loglevel",
        "error",
        "-y",
        "-i",
        input_file,
        "-vn",
        "-ac",
        "1",
        "-ar",
        "16000",
        "-c:a",
        "pcm_s16le",
        "-f",
        "wav",
        staging_path,
    };

    int exit_code = 0;
    try {
        exit_code = run_process(ffmpeg_executable, arguments);
    } catch (const ProcessError& error) {
        throw AudioConversionError(std::string("could not run FFmpeg: ") + error.what());
    }
    if (exit_code != 0) {
        throw AudioConversionError("FFmpeg failed with exit code "
                                   + std::to_string(exit_code));
    }
    if (!std::filesystem::is_regular_file(staging_path)
        || std::filesystem::file_size(staging_path) == 0U) {
        throw AudioConversionError("FFmpeg did not create a non-empty WAV file");
    }

    std::error_code error;
    if (std::filesystem::exists(output_file, error)) {
        error.clear();
        if (!std::filesystem::remove(output_file, error) || error) {
            throw AudioConversionError("could not replace the converted audio file: "
                                       + output_file.string());
        }
    } else if (error) {
        throw AudioConversionError("could not inspect the converted audio path: "
                                   + output_file.string());
    }

    std::filesystem::rename(staging_path, output_file, error);
    if (error) {
        throw AudioConversionError("could not move the converted audio into place: "
                                   + output_file.string());
    }
    partial_file.commit();
}

} // namespace vibescriber
