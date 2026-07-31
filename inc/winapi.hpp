#pragma once

#include <filesystem>
#include <string>

auto isLightTheme() -> bool;
auto hideConsole() -> void;
auto openWebpage(std::string url) -> void;
[[nodiscard]] auto getAppDataDirectory() -> std::filesystem::path;
