#pragma once

#include "download.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace vibescriber {

struct RuntimeAsset
{
    std::string display_name;
    std::string url;
    std::string sha256;
    std::filesystem::path relative_path;
    std::uintmax_t expected_bytes;
};

[[nodiscard]] const RuntimeAsset& transcription_model_asset();

[[nodiscard]] std::filesystem::path runtime_asset_path(
    const std::filesystem::path& data_directory,
    const RuntimeAsset& asset);

[[nodiscard]] DownloadResult ensure_runtime_asset(
    const std::filesystem::path& data_directory,
    const RuntimeAsset& asset,
    const DownloadProgress& progress = {});

} // namespace vibescriber
