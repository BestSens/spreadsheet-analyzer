#pragma once

#include <ctime>
#include <string>
#include <vector>

struct data_dict_t {
	std::string name;
	std::string uuid;
	std::string unit;
	bool visible{false};

	std::vector<time_t> timestamp{};
	std::vector<double> data{};
};

struct immediate_dict {
	std::string name;
	std::string unit;

	std::vector<std::pair<time_t, double>> data{};
};