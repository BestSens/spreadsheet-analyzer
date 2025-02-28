#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dicts.hpp"

auto plotDataInSubplots(const std::vector<data_dict_t> &data, size_t max_data_points, const std::string &uuid) -> void;