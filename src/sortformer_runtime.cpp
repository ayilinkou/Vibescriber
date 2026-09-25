#include "sortformer_runtime.hpp"

#include "archive_extract.hpp"

#include <stdexcept>

namespace vibescriber {
namespace {

struct Package
{
    const char* archive_name;
    const char* sha256;
    const char* library_relative;
};

Package package(const bool use_vulkan)
{
#ifdef _WIN32
    if (use_vulkan) {
        return {"nemo-speech-0.1.0-windows-x86_64-vulkan.zip",
                "b5e7b04a637da4eb25a60253e2db65774998e8dfb48c08b4db763009b82ac7ac",
                "bin/nemo_speech_asr_c.dll"};
    }
    return {"nemo-speech-0.1.0-windows-x86_64-cpu.zip",
            "5e4ea81046012edcd77fd8848de8eefb5a4ba38cc26f52eb544ab184695a75d6",
            "bin/nemo_speech_asr_c.dll"};
#else
    if (use_vulkan) {
        return {"nemo-speech-0.1.0-linux-x86_64-vulkan.tar.gz",
                "ce7b7c3c8771cb7450b26e6d4bd8fb2c5e35bcd9fe0076387f35052e9b9523ae",
                "lib/libnemo_speech_asr_c.so.1"};
    }
    return {"nemo-speech-0.1.0-linux-x86_64-cpu.tar.gz",
            "0f74131d631ad2c694cf0ec53490866bb6461147959589a69fb6fc231944065b",
            "lib/libnemo_speech_asr_c.so.1"};
#endif
}

} // namespace

std::filesystem::path ensure_sortformer_library(
    const std::filesystem::path& data_directory,
    const bool use_vulkan,
    const DownloadProgress& progress)
{
    const auto selected = package(use_vulkan);
    const auto runtime = data_directory / "tools" / selected.archive_name;
    const auto library = runtime / selected.library_relative;
    if (std::filesystem::is_regular_file(library)) {
        return library;
    }
    const auto archive_path = data_directory / "downloads" / selected.archive_name;
    const std::string url = std::string("https://github.com/NVIDIA/NeMo-Speech.cpp/")
                            + "releases/download/v0.1.0/" + selected.archive_name;
    (void)download_verified(url, archive_path, selected.sha256, progress);
#ifdef _WIN32
    extract_archive_directory(archive_path, runtime, false);
#else
    extract_archive_directory(archive_path, runtime, true);
#endif
    if (!std::filesystem::is_regular_file(library)) {
        throw std::runtime_error("the Sortformer runtime archive is missing its library");
    }
    return library;
}

} // namespace vibescriber
