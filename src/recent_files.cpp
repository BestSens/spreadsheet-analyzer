#include "recent_files.hpp"

#include <algorithm>
#include <fstream>

#include "fmt/format.h"
#include "spdlog/spdlog.h"
#include "winapi.hpp"

namespace {
	constexpr size_t max_recent_files = 10;
	constexpr char path_separator = '\t';

	auto getRecentFilesListPath() -> std::filesystem::path {
		return getAppDataDirectory() / "recent_files.txt";
	}

	// Files are always written/read as UTF-8, independent of the current locale.
	auto pathFromUtf8(const std::string &utf8) -> std::filesystem::path {
		return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
	}

	auto pathToUtf8(const std::filesystem::path &path) -> std::string {
		const auto u8 = path.u8string();
		return std::string(u8.begin(), u8.end());
	}

	auto saveRecentFiles(const std::vector<recent_file_entry_t> &recent_files) -> void {
		const auto dir = getAppDataDirectory();

		std::error_code ec;
		std::filesystem::create_directories(dir, ec);
		if (ec) {
			spdlog::warn("Failed to create app data directory '{}': {}", dir.string(), ec.message());
			return;
		}

		std::ofstream file(getRecentFilesListPath(), std::ios::trunc | std::ios::binary);
		if (!file.is_open()) {
			spdlog::warn("Failed to open recent files list for writing");
			return;
		}

		for (const auto &entry : recent_files) {
			for (size_t i = 0; i < entry.paths.size(); ++i) {
				if (i > 0) {
					file << path_separator;
				}
				file << pathToUtf8(entry.paths[i]);
			}
			file << '\n';
		}
	}

	// Path elements from root to leaf, e.g. {"C:", "Users", "foo", "file.csv"}.
	auto pathComponents(const std::filesystem::path &path) -> std::vector<std::string> {
		std::vector<std::string> parts;
		for (const auto &part : path) {
			const auto u8 = part.u8string();
			std::string str(u8.begin(), u8.end());
			if (str.empty() || str == "/" || str == "\\") {
				continue;
			}
			parts.push_back(std::move(str));
		}
		return parts;
	}

	// Joins the last `count` elements of `parts` back into a path-like string.
	auto joinTail(const std::vector<std::string> &parts, size_t count) -> std::string {
		count = std::min(count, parts.size());
		std::string result;
		for (size_t i = parts.size() - count; i < parts.size(); ++i) {
			if (!result.empty()) {
				result += '/';
			}
			result += parts[i];
		}
		return result;
	}

	auto baseLabel(const recent_file_entry_t &entry) -> std::string {
		if (entry.paths.empty()) {
			return {};
		}

		if (entry.paths.size() == 1) {
			return entry.paths.front().filename().string();
		}

		return fmt::format("{} ({} files)", entry.paths.front().parent_path().filename().string(), entry.paths.size());
	}

	// The path whose trailing folder components can be used to disambiguate this entry
	// from others that share the same base label (the file itself, or the containing
	// folder for a multi-file entry).
	auto disambiguationPath(const recent_file_entry_t &entry) -> std::filesystem::path {
		if (entry.paths.empty()) {
			return {};
		}

		if (entry.paths.size() == 1) {
			return entry.paths.front();
		}

		return entry.paths.front().parent_path();
	}

	auto findConflicting(const std::vector<std::string> &labels, size_t i) -> std::vector<size_t> {
		std::vector<size_t> conflicting{i};
		for (size_t j = i + 1; j < labels.size(); ++j) {
			if (labels[j] == labels[i]) {
				conflicting.push_back(j);
			}
		}
		return conflicting;
	}

	// Smallest number of trailing path components that makes every entry in `parts` unique
	// (or all of them, if they can't be told apart even at full path length).
	auto minDisambiguatingCount(const std::vector<std::vector<std::string>> &parts) -> size_t {
		size_t max_parts = 0;
		for (const auto &p : parts) {
			max_parts = std::max(max_parts, p.size());
		}

		for (size_t count = 1; count <= max_parts; ++count) {
			std::vector<std::string> tails;
			tails.reserve(parts.size());
			for (const auto &p : parts) {
				tails.push_back(joinTail(p, count));
			}
			std::ranges::sort(tails);
			if (std::ranges::adjacent_find(tails) == tails.end()) {
				return count;
			}
		}

		return max_parts;
	}

	// Rewrites labels[idx] for every idx in `conflicting`, prepending just enough of the
	// containing folder structure to tell the entries apart.
	auto disambiguateGroup(std::vector<std::string> &labels, const std::vector<recent_file_entry_t> &recent_files,
							const std::vector<size_t> &conflicting) -> void {
		std::vector<std::vector<std::string>> parts;
		parts.reserve(conflicting.size());
		for (const auto idx : conflicting) {
			parts.push_back(pathComponents(disambiguationPath(recent_files[idx])));
		}

		const auto count = minDisambiguatingCount(parts);

		for (size_t k = 0; k < conflicting.size(); ++k) {
			const auto idx = conflicting[k];
			const auto tail = joinTail(parts[k], count);

			labels[idx] = (recent_files[idx].paths.size() == 1)
							  ? tail
							  : fmt::format("{} ({} files)", tail, recent_files[idx].paths.size());
		}
	}
}  // namespace

auto loadRecentFiles() -> std::vector<recent_file_entry_t> {
	std::vector<recent_file_entry_t> result{};

	std::ifstream file(getRecentFilesListPath(), std::ios::binary);
	if (!file.is_open()) {
		return result;
	}

	std::string line;
	while (result.size() < max_recent_files && std::getline(file, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}

		if (line.empty()) {
			continue;
		}

		std::vector<std::filesystem::path> paths{};
		size_t start = 0;
		while (start <= line.size()) {
			const auto end = line.find(path_separator, start);
			const auto token = line.substr(start, end - start);

			if (!token.empty()) {
				auto path = pathFromUtf8(token);
				if (std::filesystem::exists(path)) {
					paths.push_back(std::move(path));
				}
			}

			if (end == std::string::npos) {
				break;
			}
			start = end + 1;
		}

		if (!paths.empty()) {
			result.push_back(recent_file_entry_t{.paths = std::move(paths)});
		}
	}

	return result;
}

auto addRecentFiles(std::vector<recent_file_entry_t> &recent_files,
					 const std::vector<std::filesystem::path> &new_entry) -> void {
	if (new_entry.empty()) {
		return;
	}

	std::vector<std::filesystem::path> absolute_paths{};
	absolute_paths.reserve(new_entry.size());
	for (const auto &path : new_entry) {
		std::error_code ec;
		auto absolute = std::filesystem::absolute(path, ec);
		absolute_paths.push_back(ec ? path : absolute);
	}

	std::erase_if(recent_files, [&absolute_paths](const recent_file_entry_t &entry) -> bool {
		return entry.paths == absolute_paths;
	});
	recent_files.insert(recent_files.begin(), recent_file_entry_t{.paths = std::move(absolute_paths)});

	if (recent_files.size() > max_recent_files) {
		recent_files.resize(max_recent_files);
	}

	saveRecentFiles(recent_files);
}

auto clearRecentFiles(std::vector<recent_file_entry_t> &recent_files) -> void {
	recent_files.clear();
	saveRecentFiles(recent_files);
}

auto getRecentFileLabels(const std::vector<recent_file_entry_t> &recent_files) -> std::vector<std::string> {
	std::vector<std::string> labels;
	labels.reserve(recent_files.size());
	for (const auto &entry : recent_files) {
		labels.push_back(baseLabel(entry));
	}

	for (size_t i = 0; i < recent_files.size(); ++i) {
		const auto conflicting = findConflicting(labels, i);
		if (conflicting.size() > 1) {
			disambiguateGroup(labels, recent_files, conflicting);
		}
	}

	return labels;
}
