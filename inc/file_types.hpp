#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

enum class file_type_t : uint8_t {
	CSV,
	BINARY
};

// Determines the type of a file from its extension. Returns nullopt for anything we cannot load.
[[nodiscard]] auto detectFileType(const std::filesystem::path& path) -> std::optional<file_type_t>;

// Resolves shortcuts, expands directories to the loadable files they contain and sorts by filename.
[[nodiscard]] auto preparePaths(std::vector<std::filesystem::path> paths) -> std::vector<std::filesystem::path>;

// Splits already prepared paths into one group per file type, preserving order. Paths of unknown
// type are dropped. Each group becomes its own window.
[[nodiscard]] auto groupPathsByType(const std::vector<std::filesystem::path>& paths)
	-> std::vector<std::pair<file_type_t, std::vector<std::filesystem::path>>>;
