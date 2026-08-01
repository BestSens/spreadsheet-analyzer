#include "file_types.hpp"

#include <algorithm>
#include <cctype>
#include <string>

#include "winapi.hpp"

namespace {
	auto toLower(std::string value) -> std::string {
		std::ranges::transform(value, value.begin(),
							   [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });
		return value;
	}
}  // namespace

auto detectFileType(const std::filesystem::path& path) -> std::optional<file_type_t> {
	const auto extension = toLower(path.extension().string());

	if (extension == ".csv") {
		return file_type_t::CSV;
	}

	if (extension == ".bin") {
		return file_type_t::BINARY;
	}

	return std::nullopt;
}

auto preparePaths(std::vector<std::filesystem::path> paths) -> std::vector<std::filesystem::path> {
	std::vector<std::filesystem::path> files{};
	files.reserve(paths.size());

	std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) {
		return a.filename() < b.filename();
	});

	for (auto& path : paths) {
		// resolve Windows shortcuts (.lnk) to their target; std::filesystem already
		// follows real symlinks/junctions transparently via is_directory()/status()
		path = resolveShortcut(path);

		if (std::filesystem::is_directory(path)) {
			std::vector<std::filesystem::path> directory_files{};

			for (const auto& entry : std::filesystem::directory_iterator(path)) {
				if (detectFileType(entry.path()).has_value()) {
					directory_files.push_back(entry.path());
				}
			}

			std::sort(directory_files.begin(), directory_files.end(), [](const auto& a, const auto& b) {
				return a.filename() < b.filename();
			});

			files.insert(files.end(), directory_files.begin(), directory_files.end());
		} else {
			files.push_back(path);
		}
	}

	return files;
}

auto groupPathsByType(const std::vector<std::filesystem::path>& paths)
	-> std::vector<std::pair<file_type_t, std::vector<std::filesystem::path>>> {
	std::vector<std::pair<file_type_t, std::vector<std::filesystem::path>>> groups{};

	for (const auto& path : paths) {
		// Files named explicitly keep the historical behaviour of being handed to the CSV
		// loader whatever their extension; only directory expansion filters strictly.
		const auto type = detectFileType(path).value_or(file_type_t::CSV);

		const auto it =
			std::ranges::find(groups, type, &std::pair<file_type_t, std::vector<std::filesystem::path>>::first);

		if (it != groups.end()) {
			it->second.push_back(path);
		} else {
			groups.emplace_back(type, std::vector<std::filesystem::path>{path});
		}
	}

	return groups;
}
