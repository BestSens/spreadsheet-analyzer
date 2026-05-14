#pragma once

#include <atomic>
#include <filesystem>
#include <optional>
#include <vector>

#include "dicts.hpp"

auto preparePaths(std::vector<std::filesystem::path> paths) -> std::vector<std::filesystem::path>;
auto loadCSVs(const std::vector<std::filesystem::path>& paths, size_t& finished, const std::atomic<bool>& stop_loading,
			  const csv_parse_config_t& config, std::string& parse_error_out) -> std::vector<data_dict_t>;
auto inferCSVParseConfig(const std::vector<std::filesystem::path>& paths,
						 const csv_parse_config_t& current_config) -> std::optional<csv_parse_config_t>;