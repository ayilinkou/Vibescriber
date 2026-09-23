#include "transcript_output.hpp"
#include "speaker_alignment.hpp"

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

void test_sortformer_speaker_labels(TestSuite& suite)
{
    const std::vector<vibescriber::TranscriptSegment> segments{
        {.text = "Hello", .start_centiseconds = 0, .end_centiseconds = 40,
         .speaker_turn_after = false, .speaker_id = 1},
        {.text = "there.", .start_centiseconds = 40, .end_centiseconds = 80,
         .speaker_turn_after = false, .speaker_id = 1},
        {.text = "Hi!", .start_centiseconds = 80, .end_centiseconds = 120,
         .speaker_turn_after = false, .speaker_id = 2},
    };
    suite.expect(vibescriber::format_transcript(segments, false)
                     == "Speaker 1: Hello there.\n\nSpeaker 2: Hi!\n",
                 "speaker identity changes start labeled paragraphs");
}

void test_speaker_alignment(TestSuite& suite)
{
    std::vector<vibescriber::TranscriptSegment> words{
        {.text = "one", .start_centiseconds = 10, .end_centiseconds = 45,
         .speaker_turn_after = false},
        {.text = "two", .start_centiseconds = 55, .end_centiseconds = 95,
         .speaker_turn_after = false},
        {.text = "three", .start_centiseconds = 300, .end_centiseconds = 330,
         .speaker_turn_after = false},
    };
    vibescriber::assign_speakers(words, {{0, 50, 1}, {50, 110, 2}});
    suite.expect(words[0].speaker_id == 1 && words[1].speaker_id == 2
                     && words[2].speaker_id == 0,
                 "words align by overlap and distant words remain unlabeled");
}

void test_overlap_uses_frame_probabilities(TestSuite& suite)
{
    std::vector<vibescriber::TranscriptSegment> words{
        {.text = "Yeah,", .start_centiseconds = 2040,
         .end_centiseconds = 2086, .speaker_turn_after = false},
    };
    vibescriber::DiarizationResult result;
    result.intervals = {{1800, 2088, 2}, {2041, 2840, 1}};
    result.frame_probabilities.resize(261);
    result.frame_probabilities[257] = {0.519F, 0.962F, 0.0F, 0.0F};
    result.frame_probabilities[258] = {0.862F, 0.871F, 0.0F, 0.0F};
    result.frame_probabilities[259] = {0.993F, 0.605F, 0.0F, 0.0F};
    result.frame_probabilities[260] = {0.997F, 0.441F, 0.0F, 0.0F};
    vibescriber::assign_speakers(words, result);
    suite.expect(words[0].speaker_id == 1,
                 "frame probabilities resolve an overlapping speaker boundary");
}

void test_diarization_result_reading(TestSuite& suite)
{
    TemporaryDirectory directory;
    const auto path = directory.path() / "speakers.tsv";
    {
        std::ofstream output(path);
        output << "S\t0.0800\t1.2000\t2\n"
               << "P\t0\t0.100000\t0.900000\t0.000000\t0.000000\n";
    }
    const auto result = vibescriber::read_diarization_result(path);
    suite.expect(result.intervals.size() == 1U
                     && result.intervals[0].start_centiseconds == 8
                     && result.intervals[0].speaker_id == 2
                     && result.frame_probabilities.size() == 1U
                     && result.frame_probabilities[0][1] > 0.89F,
                 "Sortformer intervals and frame probabilities are read together");
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
    test_sortformer_speaker_labels(suite);
    test_speaker_alignment(suite);
    test_overlap_uses_frame_probabilities(suite);
    test_diarization_result_reading(suite);
    test_atomic_write(suite);

    if (suite.failures() != 0) {
        std::cerr << suite.failures() << " transcript output test assertion(s) failed\n";
        return 1;
    }

    std::cout << "All transcript output tests passed\n";
    return 0;
}
