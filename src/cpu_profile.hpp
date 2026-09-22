#pragma once

namespace vibescriber {

enum class CpuProfile
{
    slow,
    balanced,
    fast,
};

[[nodiscard]] int transcription_thread_count(
    CpuProfile profile,
    unsigned int hardware_concurrency);

} // namespace vibescriber
