#pragma once

#include <atomic>
#include <filesystem>
#include <vector>

#include "dicts.hpp"

auto preparePaths(std::vector<std::filesystem::path> paths) -> std::vector<std::filesystem::path>;
auto loadCSVs(const std::vector<std::filesystem::path> &paths, std::atomic<size_t> &finished,
			  std::atomic<bool> &stop_loading, bool parallel_loading) -> std::vector<data_dict_t>;