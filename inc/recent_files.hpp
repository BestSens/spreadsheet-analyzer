#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "uuid.h"
#include "uuid_generator.hpp"

// One entry in the recent files list. Usually a single file, but may hold several
// paths when a group of files (or a folder) was opened together as one window.
// Carries its own UUID so ImGui widget IDs stay unique even when two entries
// happen to render the same label (e.g. same filename from different folders).
struct recent_file_entry_t {
	// NOLINTBEGIN(misc-non-private-member-variables-in-classes)
	std::vector<std::filesystem::path> paths;
	uuids::uuid id{UUIDGenerator::getInstance().generate()};
	// NOLINTEND(misc-non-private-member-variables-in-classes)

	[[nodiscard]] auto getID() const -> std::string {
		return uuids::to_string(id);
	}

	auto operator==(const recent_file_entry_t &other) const -> bool {
		return paths == other.paths;
	}
};

[[nodiscard]] auto loadRecentFiles() -> std::vector<recent_file_entry_t>;
auto addRecentFiles(std::vector<recent_file_entry_t> &recent_files,
					 const std::vector<std::filesystem::path> &new_entry) -> void;
auto clearRecentFiles(std::vector<recent_file_entry_t> &recent_files) -> void;
// Returns one display label per entry (same order/size as recent_files), disambiguating
// entries that would otherwise share a label by prepending the first differing folder.
[[nodiscard]] auto getRecentFileLabels(const std::vector<recent_file_entry_t> &recent_files) -> std::vector<std::string>;
