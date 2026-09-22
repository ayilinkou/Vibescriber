#include "cpu_profile.hpp"

#include <algorithm>
#include <limits>

namespace vibescriber {

int transcription_thread_count(
    const CpuProfile profile,
    const unsigned int hardware_concurrency)
{
    const unsigned int available = std::max(1U, hardware_concurrency);
    unsigned int selected = available / 2U;
    if (profile == CpuProfile::slow) {
        selected = available / 4U;
    } else if (profile == CpuProfile::fast) {
        selected = available;
    }
    selected = std::max(1U, selected);
    const unsigned int maximum = static_cast<unsigned int>(
        std::numeric_limits<int>::max());
    return static_cast<int>(std::min(selected, maximum));
}

} // namespace vibescriber
