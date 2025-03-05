#pragma once

#include <ctime>
#include <limits>
#include <string>
#include <vector>

enum class data_type_t : uint8_t {
	FLOAT,
	BOOLEAN
};

struct data_aggregate_t {
	time_t date;
	double min;
	double max;
	double mean;
	double std;
	double first;
};

struct data_dict_t {
	std::string name;
	std::string uuid;
	std::string unit;
	bool visible{false};
	data_type_t data_type{data_type_t::FLOAT};

	std::vector<time_t> timestamp{};
	std::vector<double> data{};

	size_t aggregated_to{0};
	std::vector<data_aggregate_t> aggregates{};
	std::pair<double, double> fit_zoom_range{std::numeric_limits<double>::quiet_NaN(),
											 std::numeric_limits<double>::quiet_NaN()};
	int fit_zoom_calculated_for_points{0};
};

struct immediate_dict {
	std::string name;
	std::string unit;

	std::vector<std::pair<time_t, double>> data{};
};