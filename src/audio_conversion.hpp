#pragma once

#include <filesystem>
#include <stdexcept>

namespace vibescriber {

class AudioConversionError final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

void convert_audio_to_wav(
    const std::filesystem::path& ffmpeg_executable,
    const std::filesystem::path& input_file,
    const std::filesystem::path& output_file);

} // namespace vibescriber
