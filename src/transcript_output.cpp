#include "transcript_output.hpp"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace vibescriber {
namespace {

constexpr std::int64_t paragraph_pause_centiseconds = 150;

struct Paragraph
{
    std::string text;
    std::int64_t start_centiseconds = 0;
    std::int64_t end_centiseconds = 0;
};

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

[[nodiscard]] std::string_view trim(const std::string_view text)
{
    std::size_t first = 0;
    while (first < text.size()
           && std::isspace(static_cast<unsigned char>(text[first])) != 0) {
        ++first;
    }

    std::size_t last = text.size();
    while (last > first
           && std::isspace(static_cast<unsigned char>(text[last - 1U])) != 0) {
        --last;
    }
    return text.substr(first, last - first);
}

[[nodiscard]] bool attaches_to_previous(const char character)
{
    constexpr std::string_view punctuation = ".,!?;:%)]}";
    return punctuation.find(character) != std::string_view::npos;
}

void append_segment_text(std::string& paragraph, const std::string_view text)
{
    if (!paragraph.empty() && !attaches_to_previous(text.front())) {
        paragraph.push_back(' ');
    }
    paragraph.append(text);
}

[[nodiscard]] std::string timestamp(const std::int64_t centiseconds)
{
    const std::int64_t clamped = centiseconds < 0 ? 0 : centiseconds;
    const std::int64_t hours = clamped / 360'000;
    const std::int64_t minutes = (clamped / 6'000) % 60;
    const std::int64_t seconds = (clamped / 100) % 60;
    const std::int64_t fraction = clamped % 100;

    std::ostringstream output;
    output << std::setfill('0') << std::setw(2) << hours << ':'
           << std::setw(2) << minutes << ':' << std::setw(2) << seconds << '.'
           << std::setw(2) << fraction;
    return output.str();
}

} // namespace

std::string format_transcript(
    const std::span<const TranscriptSegment> segments,
    const bool include_timestamps)
{
    std::vector<Paragraph> paragraphs;
    Paragraph current;
    bool previous_speaker_turn = false;
    std::int64_t previous_end = 0;
    bool have_previous_timing = false;

    const auto finish_paragraph = [&] {
        if (!current.text.empty()) {
            paragraphs.push_back(std::move(current));
            current = Paragraph{};
        }
    };

    for (const auto& segment : segments) {
        const std::string_view text = trim(segment.text);
        const bool long_pause = have_previous_timing
                                && segment.start_centiseconds - previous_end
                                       >= paragraph_pause_centiseconds;
        if (!current.text.empty() && (previous_speaker_turn || long_pause)) {
            finish_paragraph();
        }

        if (!text.empty()) {
            if (current.text.empty()) {
                current.start_centiseconds = segment.start_centiseconds;
            }
            append_segment_text(current.text, text);
            current.end_centiseconds = segment.end_centiseconds;
        }

        previous_speaker_turn = segment.speaker_turn_after;
        previous_end = segment.end_centiseconds;
        have_previous_timing = true;
        if (segment.speaker_turn_after) {
            finish_paragraph();
        }
    }
    finish_paragraph();

    std::ostringstream output;
    for (std::size_t index = 0; index < paragraphs.size(); ++index) {
        const auto& paragraph = paragraphs[index];
        if (index != 0U) {
            output << "\n\n";
        }
        if (include_timestamps) {
            output << '[' << timestamp(paragraph.start_centiseconds) << " --> "
                   << timestamp(paragraph.end_centiseconds) << "] ";
        }
        output << paragraph.text;
    }
    if (!paragraphs.empty()) {
        output << '\n';
    }
    return output.str();
}

void write_transcript(
    const std::filesystem::path& path,
    const std::string& contents)
{
    if (path.empty()) {
        throw TranscriptOutputError("the transcript output path may not be empty");
    }
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    std::filesystem::path staging_path = path;
    staging_path += ".part";
    PartialFile partial_file(staging_path);

    std::ofstream output(staging_path, std::ios::binary | std::ios::trunc);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    if (!output) {
        throw TranscriptOutputError("failed to write the temporary transcript file: "
                                    + staging_path.string());
    }

    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        error.clear();
        if (!std::filesystem::remove(path, error) || error) {
            throw TranscriptOutputError("could not replace the transcript output: "
                                        + path.string());
        }
    } else if (error) {
        throw TranscriptOutputError("could not inspect the transcript output: "
                                    + path.string());
    }

    std::filesystem::rename(staging_path, path, error);
    if (error) {
        throw TranscriptOutputError("could not move the transcript into place: "
                                    + path.string());
    }
    partial_file.commit();
}

} // namespace vibescriber
