#include "file_dialog.hpp"

#include <array>
#include <filesystem>
#include <vector>

#include "nfd.hpp"
#include "spdlog/spdlog.h"

auto selectFilesFromDialog(bool select_folder) -> std::vector<std::filesystem::path> {
	const NFD::Guard nfd_guard{};
	NFD::UniquePathSet out_paths{};

	const auto filters = std::array<nfdfilteritem_t, 1>{nfdfilteritem_t{"CSV", "csv"}};

	const auto result = [&]() {
		if (!select_folder) {
			return NFD::OpenDialogMultiple(out_paths, filters.data(), filters.size());
		} else {
			const nfdu8char_t *default_path = nullptr;
			return NFD::PickFolderMultiple(out_paths, default_path);
		}
	}();

	if (result == NFD_OKAY) {
		nfdpathsetsize_t num_paths{};
		NFD::PathSet::Count(out_paths, num_paths);
		std::vector<std::filesystem::path> paths{};
		paths.reserve(num_paths);

		for (nfdpathsetsize_t i = 0; i < num_paths; ++i) {
			NFD::UniquePathSetPath path;
			NFD::PathSet::GetPath(out_paths, i, path);
			paths.push_back(std::filesystem::path(path.get()));
		}

		return paths;
	} else if (result == NFD_CANCEL) {
		spdlog::debug("User pressed cancel.");
	} else {
		spdlog::error("Error: {}", NFD::GetError());
	}

	return {};
}