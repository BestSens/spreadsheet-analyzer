#pragma once

#include <string>
#include <string_view>

auto trim(std::string_view str) -> std::string_view;
auto stripUnit(std::string_view header) -> std::pair<std::string, std::string>;
auto getIncrementedWindowTitle(const std::string &title) -> std::string;