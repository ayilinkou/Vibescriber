#include "transcription.hpp"
#include "wav_reader.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

class TestSuite
{
public:
    void expect(const bool condition, const std::string_view description)
    {
        if (!condition) {
            ++failures_;
            std::cerr << "FAIL: " << description << '\n';
        }
    }

    template <typename Exception, typename Function>
    void expect_error(Function&& function, const std::string_view description)
    {
        try {
            function();
            expect(false, description);
        } catch (const Exception&) {
        }
    }

    [[nodiscard]] int failures() const
    {
        return failures_;
    }

private:
    int failures_ = 0;
};

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        const auto unique_value = std::chrono::steady_clock::now()
                                      .time_since_epoch()
                                      .count();
        path_ = std::filesystem::temp_directory_path()
                / ("vibescriber-transcription-tests-" + std::to_string(unique_value));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void write_u16(std::ofstream& output, const std::uint16_t value)
{
    output.put(static_cast<char>(value & 0xffU));
    output.put(static_cast<char>((value >> 8U) & 0xffU));
}

void write_u32(std::ofstream& output, const std::uint32_t value)
{
    output.put(static_cast<char>(value & 0xffU));
    output.put(static_cast<char>((value >> 8U) & 0xffU));
    output.put(static_cast<char>((value >> 16U) & 0xffU));
    output.put(static_cast<char>((value >> 24U) & 0xffU));
}

void write_wav(
    const std::filesystem::path& path,
    const std::uint32_t sample_rate,
    const std::array<std::int16_t, 3>& samples)
{
    constexpr std::uint32_t data_size = 6U;
    std::ofstream output(path, std::ios::binary);
    output.write("RIFF", 4);
    write_u32(output, 36U + data_size);
    output.write("WAVE", 4);
    output.write("fmt ", 4);
    write_u32(output, 16U);
    write_u16(output, 1U);
    write_u16(output, 1U);
    write_u32(output, sample_rate);
    write_u32(output, sample_rate * 2U);
    write_u16(output, 2U);
    write_u16(output, 16U);
    output.write("data", 4);
    write_u32(output, data_size);
    for (const auto sample : samples) {
        write_u16(output, static_cast<std::uint16_t>(sample));
    }
    if (!output) {
        throw std::runtime_error("failed to write a test WAV");
    }
}

void write_text(const std::filesystem::path& path, const std::string_view text)
{
    std::ofstream output(path, std::ios::binary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        throw std::runtime_error("failed to write a test file");
    }
}

void test_wav_reader(TestSuite& suite)
{
    TemporaryDirectory temporary_directory;
    const auto wav = temporary_directory.path() / "audio.wav";
    write_wav(wav, 16'000U, {std::int16_t{-32768}, std::int16_t{0}, std::int16_t{16384}});

    const std::vector<float> samples = vibescriber::read_mono_16khz_pcm16_wav(wav);
    suite.expect(samples.size() == 3U, "all WAV samples are read");
    suite.expect(std::abs(samples[0] + 1.0F) < 0.0001F,
                 "negative PCM is normalized to floating point");
    suite.expect(std::abs(samples[1]) < 0.0001F,
                 "zero PCM is normalized to floating point");
    suite.expect(std::abs(samples[2] - 0.5F) < 0.0001F,
                 "positive PCM is normalized to floating point");

    const auto wrong_rate = temporary_directory.path() / "wrong-rate.wav";
    write_wav(wrong_rate, 44'100U, {0, 0, 0});
    suite.expect_error<vibescriber::WavError>(
        [&] { (void)vibescriber::read_mono_16khz_pcm16_wav(wrong_rate); },
        "unexpected WAV formats are rejected");
}

void test_whisper_is_linked(TestSuite& suite)
{
    TemporaryDirectory temporary_directory;
    const auto wav = temporary_directory.path() / "audio.wav";
    const auto invalid_model = temporary_directory.path() / "model.bin";
    write_wav(wav, 16'000U, {0, 0, 0});
    write_text(invalid_model, "not a Whisper model");

    suite.expect_error<vibescriber::TranscriptionError>(
        [&] { (void)vibescriber::transcribe_wav(invalid_model, wav); },
        "the whisper.cpp wrapper rejects an invalid model");
}

} // namespace

int main()
{
    TestSuite suite;
    test_wav_reader(suite);
    test_whisper_is_linked(suite);

    if (suite.failures() != 0) {
        std::cerr << suite.failures() << " transcription test assertion(s) failed\n";
        return 1;
    }

    std::cout << "All transcription tests passed\n";
    return 0;
}
