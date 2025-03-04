#pragma once

#include <ctime>
#include <string>
#include <vector>

enum class data_type_t : uint8_t {
	FLOAT,
	BOOLEAN
};

struct data_dict_t {
	std::string name;
	std::string uuid;
	std::string unit;
	bool visible{false};
	data_type_t data_type{data_type_t::FLOAT};

	std::vector<time_t> timestamp{};
	std::vector<double> data{};
};

struct immediate_dict {
	std::string name;
	std::string unit;

	std::vector<std::pair<time_t, double>> data{};
};