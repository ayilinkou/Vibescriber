#include "runtime_asset.hpp"

#include <stdexcept>

namespace vibescriber {
namespace {

bool is_safe_relative_path(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute()) {
        return false;
    }
    for (const std::filesystem::path& component : path) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

} // namespace

const RuntimeAsset& transcription_model_asset()
{
    static const RuntimeAsset asset{
        .display_name = "Whisper small.en TinyDiarize model",
        .url = "https://huggingface.co/akashmjn/tinydiarize-whisper.cpp/resolve/"
               "d44ba793fc67e509623a88a409723311fa677744/ggml-small.en-tdrz.bin",
        .sha256 = "ceac3ec06d1d98ef71aec665283564631055fd6129b79d8e1be4f9cc33cc54b4",
        .relative_path = "models/ggml-small.en-tdrz.bin",
        .expected_bytes = 487'614'184U,
    };
    return asset;
}

std::filesystem::path runtime_asset_path(
    const std::filesystem::path& data_directory,
    const RuntimeAsset& asset)
{
    if (data_directory.empty()) {
        throw std::invalid_argument("the application data directory may not be empty");
    }
    if (!is_safe_relative_path(asset.relative_path)) {
        throw std::invalid_argument("a runtime asset path must be a safe relative path");
    }
    return data_directory / asset.relative_path;
}

DownloadResult ensure_runtime_asset(
    const std::filesystem::path& data_directory,
    const RuntimeAsset& asset)
{
    return download_verified(
        asset.url,
        runtime_asset_path(data_directory, asset),
        asset.sha256);
}

} // namespace vibescriber
