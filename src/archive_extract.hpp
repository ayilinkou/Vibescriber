#pragma once

#include <filesystem>
#include <stdexcept>

namespace vibescriber {

class ArchiveError final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

void extract_archive_member(
    const std::filesystem::path& archive_path,
    const std::filesystem::path& member_path,
    const std::filesystem::path& destination);

void extract_archive_directory(
    const std::filesystem::path& archive_path,
    const std::filesystem::path& destination,
    bool strip_first_component);

} // namespace vibescriber
