#pragma once

#include <atomic>
#include <filesystem>
#include <vector>

#include "dicts.hpp"

auto preparePaths(std::vector<std::filesystem::path> paths) -> std::vector<std::filesystem::path>;
auto loadCSVs(const std::vector<std::filesystem::path> &paths, size_t &finished, const bool &stop_loading,
			  bool parallel_loading) -> std::vector<data_dict_t>;