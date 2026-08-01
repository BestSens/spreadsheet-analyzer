#pragma once

#include <ctime>
#include <limits>
#include <memory>
#include <string>
#include <vector>

enum class data_type_t : uint8_t {
	FLOAT,
	BOOLEAN
};

struct data_aggregate_t {
	double date;
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

	// 0 = auto-assign, 1/2/3 = explicit Y1/Y2/Y3. Multiple columns may share the same axis.
	int y_axis{0};

	// unix time in seconds; double so sub-second sample rates (binary raw data) fit
	std::shared_ptr<std::vector<double>> timestamp{std::make_shared<std::vector<double>>()};
	double delta_t{};
	std::shared_ptr<std::vector<double>> data{std::make_shared<std::vector<double>>()};

	size_t aggregated_to{0};
	std::vector<data_aggregate_t> aggregates{};
	std::pair<double, double> fit_zoom_range{std::numeric_limits<double>::quiet_NaN(),
											 std::numeric_limits<double>::quiet_NaN()};
	int fit_zoom_calculated_for_points{0};
};

struct immediate_dict {
	std::string name;
	std::string unit;

	std::vector<std::pair<double, double>> data{};
};

struct csv_parse_config_t {
	char        field_delimiter   {','};
	char        decimal_separator {','};
	std::string date_format       {};    // empty = auto-detect
	size_t      date_column_index {0};
	bool        first_row_is_header {true};
};