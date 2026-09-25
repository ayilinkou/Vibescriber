#pragma once

#include "download.hpp"

#include <filesystem>

namespace vibescriber {

[[nodiscard]] std::filesystem::path ensure_sortformer_library(
    const std::filesystem::path& data_directory,
    bool use_vulkan,
    const DownloadProgress& progress = {});

} // namespace vibescriber
