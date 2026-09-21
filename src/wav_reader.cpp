#include "wav_reader.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>

namespace vibescriber {
namespace {

[[nodiscard]] std::uint16_t little_u16(const unsigned char* bytes)
{
    return static_cast<std::uint16_t>(bytes[0])
           | static_cast<std::uint16_t>(bytes[1]) << 8U;
}

[[nodiscard]] std::uint32_t little_u32(const unsigned char* bytes)
{
    return static_cast<std::uint32_t>(bytes[0])
           | static_cast<std::uint32_t>(bytes[1]) << 8U
           | static_cast<std::uint32_t>(bytes[2]) << 16U
           | static_cast<std::uint32_t>(bytes[3]) << 24U;
}

void read_exact(std::ifstream& input, char* destination, const std::streamsize size)
{
    input.read(destination, size);
    if (input.gcount() != size) {
        throw WavError("the WAV file ended unexpectedly");
    }
}

[[nodiscard]] bool chunk_id_is(
    const std::array<unsigned char, 8>& header,
    const std::string& expected)
{
    return std::string(
               reinterpret_cast<const char*>(header.data()), expected.size())
           == expected;
}

} // namespace

std::vector<float> read_mono_16khz_pcm16_wav(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw WavError("could not open the WAV file: " + path.string());
    }

    std::array<unsigned char, 12> riff_header{};
    read_exact(
        input,
        reinterpret_cast<char*>(riff_header.data()),
        static_cast<std::streamsize>(riff_header.size()));
    if (std::string(reinterpret_cast<const char*>(riff_header.data()), 4) != "RIFF"
        || std::string(reinterpret_cast<const char*>(riff_header.data() + 8), 4)
               != "WAVE") {
        throw WavError("the input is not a RIFF/WAVE file");
    }

    bool format_found = false;
    bool data_found = false;
    std::uint64_t data_offset = 0;
    std::uint32_t data_size = 0;

    while (input && !data_found) {
        std::array<unsigned char, 8> chunk_header{};
        input.read(
            reinterpret_cast<char*>(chunk_header.data()),
            static_cast<std::streamsize>(chunk_header.size()));
        if (input.gcount() == 0) {
            break;
        }
        if (input.gcount() != static_cast<std::streamsize>(chunk_header.size())) {
            throw WavError("the WAV chunk header is truncated");
        }

        const std::uint32_t chunk_size = little_u32(chunk_header.data() + 4);
        if (chunk_id_is(chunk_header, "fmt ")) {
            if (chunk_size < 16U) {
                throw WavError("the WAV format chunk is too small");
            }
            std::array<unsigned char, 16> format{};
            read_exact(
                input,
                reinterpret_cast<char*>(format.data()),
                static_cast<std::streamsize>(format.size()));
            if (little_u16(format.data()) != 1U
                || little_u16(format.data() + 2) != 1U
                || little_u32(format.data() + 4) != 16'000U
                || little_u16(format.data() + 12) != 2U
                || little_u16(format.data() + 14) != 16U) {
                throw WavError("the WAV file must be mono 16 kHz 16-bit PCM");
            }
            input.seekg(static_cast<std::streamoff>(chunk_size - 16U), std::ios::cur);
            format_found = true;
        } else if (chunk_id_is(chunk_header, "data")) {
            const std::streampos position = input.tellg();
            if (position < 0) {
                throw WavError("could not locate the WAV sample data");
            }
            data_offset = static_cast<std::uint64_t>(position);
            data_size = chunk_size;
            data_found = true;
        } else {
            input.seekg(static_cast<std::streamoff>(chunk_size), std::ios::cur);
        }

        if (!data_found && (chunk_size & 1U) != 0U) {
            input.seekg(1, std::ios::cur);
        }
        if (!input) {
            throw WavError("a WAV chunk extends beyond the end of the file");
        }
    }

    if (!format_found) {
        throw WavError("the WAV file has no format chunk before its audio data");
    }
    if (!data_found) {
        throw WavError("the WAV file has no audio data chunk");
    }
    if ((data_size & 1U) != 0U) {
        throw WavError("the WAV sample data has an invalid byte count");
    }
    if (data_size / 2U > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        throw WavError("the WAV file contains too many samples");
    }

    input.clear();
    input.seekg(static_cast<std::streamoff>(data_offset), std::ios::beg);
    std::vector<float> samples;
    samples.reserve(data_size / 2U);
    std::array<unsigned char, 2> encoded_sample{};
    for (std::uint32_t offset = 0; offset < data_size; offset += 2U) {
        read_exact(
            input,
            reinterpret_cast<char*>(encoded_sample.data()),
            static_cast<std::streamsize>(encoded_sample.size()));
        const std::uint16_t unsigned_value = little_u16(encoded_sample.data());
        const std::int32_t signed_value = unsigned_value <= 32'767U
                                              ? unsigned_value
                                              : static_cast<std::int32_t>(unsigned_value)
                                                    - 65'536;
        samples.push_back(static_cast<float>(signed_value) / 32768.0F);
    }
    return samples;
}

} // namespace vibescriber
