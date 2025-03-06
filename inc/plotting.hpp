#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dicts.hpp"

auto plotDataInSubplots(std::vector<data_dict_t> &data, const std::string &uuid,
						std::vector<std::string> assigned_plot_ids) -> std::vector<std::string>;