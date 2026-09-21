#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>

namespace vibescriber {

[[nodiscard]] std::string sha256(std::span<const std::byte> data);
[[nodiscard]] std::string sha256_file(const std::filesystem::path& path);

} // namespace vibescriber
