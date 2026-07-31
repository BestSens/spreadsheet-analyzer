#pragma once

#include <filesystem>
#include <string>
#include <vector>

// One entry in the recent files list. Usually a single file, but may hold several
// paths when a group of files (or a folder) was opened together as one window.
using recent_file_entry_t = std::vector<std::filesystem::path>;

[[nodiscard]] auto loadRecentFiles() -> std::vector<recent_file_entry_t>;
auto addRecentFiles(std::vector<recent_file_entry_t> &recent_files, const recent_file_entry_t &new_entry) -> void;
auto clearRecentFiles(std::vector<recent_file_entry_t> &recent_files) -> void;
[[nodiscard]] auto getRecentFileLabel(const recent_file_entry_t &entry) -> std::string;
