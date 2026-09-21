#include "transcript_output.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
                / ("vibescriber-output-tests-" + std::to_string(unique_value));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

[[nodiscard]] std::string read_text(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void test_speaker_paragraphs(TestSuite& suite)
{
    const std::vector<vibescriber::TranscriptSegment> segments{
        {.text = "  Hello", .start_centiseconds = 0, .end_centiseconds = 80,
         .speaker_turn_after = false},
        {.text = "there.  ", .start_centiseconds = 80, .end_centiseconds = 140,
         .speaker_turn_after = true},
        {.text = " General", .start_centiseconds = 140, .end_centiseconds = 210,
         .speaker_turn_after = false},
        {.text = "Kenobi!", .start_centiseconds = 210, .end_centiseconds = 280,
         .speaker_turn_after = false},
    };

    suite.expect(
        vibescriber::format_transcript(segments, false)
            == "Hello there.\n\nGeneral Kenobi!\n",
        "speaker turns create unlabeled paragraph breaks");
    suite.expect(
        vibescriber::format_transcript(segments, true)
            == "[00:00:00.00 --> 00:00:01.40] Hello there.\n\n"
               "[00:00:01.40 --> 00:00:02.80] General Kenobi!\n",
        "timestamps describe each readable paragraph");
}

void test_pause_paragraphs(TestSuite& suite)
{
    const std::vector<vibescriber::TranscriptSegment> segments{
        {.text = "First thought.", .start_centiseconds = 0, .end_centiseconds = 100,
         .speaker_turn_after = false},
        {.text = "Second thought.", .start_centiseconds = 260,
         .end_centiseconds = 350, .speaker_turn_after = false},
    };
    suite.expect(
        vibescriber::format_transcript(segments, false)
            == "First thought.\n\nSecond thought.\n",
        "a pause of at least 1.5 seconds creates a fallback paragraph break");
    suite.expect(vibescriber::format_transcript({}, false).empty(),
                 "an empty transcription formats as an empty file");
}

void test_atomic_write(TestSuite& suite)
{
    TemporaryDirectory temporary_directory;
    const auto output = temporary_directory.path() / "nested" / "transcript.txt";
    vibescriber::write_transcript(output, "first\n");
    suite.expect(read_text(output) == "first\n", "a transcript is written");

    vibescriber::write_transcript(output, "replacement\n");
    suite.expect(read_text(output) == "replacement\n",
                 "a transcript destination can be replaced atomically");
    suite.expect(!std::filesystem::exists(output.string() + ".part"),
                 "successful output leaves no staging file");
}

} // namespace

int main()
{
    TestSuite suite;
    test_speaker_paragraphs(suite);
    test_pause_paragraphs(suite);
    test_atomic_write(suite);

    if (suite.failures() != 0) {
        std::cerr << suite.failures() << " transcript output test assertion(s) failed\n";
        return 1;
    }

    std::cout << "All transcript output tests passed\n";
    return 0;
}
