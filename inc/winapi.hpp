#pragma once

#include <filesystem>
#include <string>

auto isLightTheme() -> bool;
auto hideConsole() -> void;
auto openWebpage(std::string url) -> void;
[[nodiscard]] auto getAppDataDirectory() -> std::filesystem::path;

// Resolves a Windows shortcut (.lnk) to the path it points to. On platforms without
// shortcut files, or if resolution fails, the input path is returned unchanged.
[[nodiscard]] auto resolveShortcut(std::filesystem::path path) -> std::filesystem::path;
