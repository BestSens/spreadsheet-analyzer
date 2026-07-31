#include "recent_files.hpp"

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
			for (size_t i = 0; i < entry.size(); ++i) {
				if (i > 0) {
					file << path_separator;
				}
				file << pathToUtf8(entry[i]);
			}
			file << '\n';
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

		recent_file_entry_t entry{};
		size_t start = 0;
		while (start <= line.size()) {
			const auto end = line.find(path_separator, start);
			const auto token = line.substr(start, end - start);

			if (!token.empty()) {
				auto path = pathFromUtf8(token);
				if (std::filesystem::exists(path)) {
					entry.push_back(std::move(path));
				}
			}

			if (end == std::string::npos) {
				break;
			}
			start = end + 1;
		}

		if (!entry.empty()) {
			result.push_back(std::move(entry));
		}
	}

	return result;
}

auto addRecentFiles(std::vector<recent_file_entry_t> &recent_files, const recent_file_entry_t &new_entry) -> void {
	if (new_entry.empty()) {
		return;
	}

	recent_file_entry_t absolute_entry{};
	absolute_entry.reserve(new_entry.size());
	for (const auto &path : new_entry) {
		std::error_code ec;
		auto absolute = std::filesystem::absolute(path, ec);
		absolute_entry.push_back(ec ? path : absolute);
	}

	std::erase(recent_files, absolute_entry);
	recent_files.insert(recent_files.begin(), std::move(absolute_entry));

	if (recent_files.size() > max_recent_files) {
		recent_files.resize(max_recent_files);
	}

	saveRecentFiles(recent_files);
}

auto clearRecentFiles(std::vector<recent_file_entry_t> &recent_files) -> void {
	recent_files.clear();
	saveRecentFiles(recent_files);
}

auto getRecentFileLabel(const recent_file_entry_t &entry) -> std::string {
	if (entry.empty()) {
		return {};
	}

	if (entry.size() == 1) {
		return entry.front().filename().string();
	}

	return fmt::format("{} ({} files)", entry.front().parent_path().filename().string(), entry.size());
}
