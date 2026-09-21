#pragma once

#include <filesystem>
#include <stdexcept>
#include <vector>

namespace vibescriber {

class WavError final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::vector<float> read_mono_16khz_pcm16_wav(
    const std::filesystem::path& path);

} // namespace vibescriber
