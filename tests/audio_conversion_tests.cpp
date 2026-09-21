#include "audio_conversion.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

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
                / ("vibescriber-audio-tests-" + std::to_string(unique_value));
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

void write_text(const std::filesystem::path& path, const std::string_view text)
{
    std::ofstream output(path, std::ios::binary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        throw std::runtime_error("failed to write a test file");
    }
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void test_conversion(
    TestSuite& suite,
    const std::filesystem::path& fixture_executable)
{
    TemporaryDirectory temporary_directory;
    const auto input = temporary_directory.path() / "input with spaces;safe.mp4";
    const auto output = temporary_directory.path() / "nested output" / "audio.wav";
    write_text(input, "media fixture");

    vibescriber::convert_audio_to_wav(fixture_executable, input, output);
    const std::string arguments = read_text(output);
    suite.expect(arguments.find("-i\n" + input.string() + "\n") != std::string::npos,
                 "the input path is passed as one literal process argument");
    suite.expect(arguments.find("-ac\n1\n-ar\n16000\n") != std::string::npos,
                 "FFmpeg is asked for mono 16 kHz audio");
    suite.expect(arguments.find("-c:a\npcm_s16le\n-f\nwav\n") != std::string::npos,
                 "FFmpeg is asked for 16-bit PCM in a WAV container");
    suite.expect(!std::filesystem::exists(output.string() + ".part"),
                 "successful conversion leaves no staging file");
}

void test_conversion_failure(
    TestSuite& suite,
    const std::filesystem::path& fixture_executable)
{
    TemporaryDirectory temporary_directory;
    const auto input = temporary_directory.path() / "fail input.mp4";
    const auto output = temporary_directory.path() / "audio.wav";
    write_text(input, "media fixture");
    write_text(output, "existing output");

    suite.expect_error<vibescriber::AudioConversionError>(
        [&] { vibescriber::convert_audio_to_wav(fixture_executable, input, output); },
        "a non-zero FFmpeg exit code rejects conversion");
    suite.expect(read_text(output) == "existing output",
                 "failed conversion preserves an existing destination");
    suite.expect(!std::filesystem::exists(output.string() + ".part"),
                 "failed conversion removes its staging file");
}

} // namespace

int main(const int argc, char* argv[])
{
    if (argc != 2) {
        std::cerr << "the process fixture path is required\n";
        return 2;
    }

    TestSuite suite;
    test_conversion(suite, argv[1]);
    test_conversion_failure(suite, argv[1]);

    if (suite.failures() != 0) {
        std::cerr << suite.failures() << " audio conversion test assertion(s) failed\n";
        return 1;
    }

    std::cout << "All audio conversion tests passed\n";
    return 0;
}
