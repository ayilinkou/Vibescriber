#pragma once

namespace vibescriber {

enum class ThemeMode { automatic = 0, light = 1, dark = 2 };

// Returns light when the desktop does not publish an appearance preference.
[[nodiscard]] bool system_prefers_dark();

} // namespace vibescriber
