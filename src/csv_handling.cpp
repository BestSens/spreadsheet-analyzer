#include "csv_handling.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <execution>
#include <filesystem>
#include <iomanip>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "csv.hpp"
#include "dicts.hpp"
#include "fast_float/fast_float.h"
#include "spdlog/spdlog.h"
#include "utility.hpp"
#include "uuid_generator.hpp"

namespace {
	const auto date_formats = std::array{
		"%Y/%m/%d %H:%M:%S",
		"%Y-%m-%d %H:%M:%S"
	};

	auto parseDate(const std::string &str, size_t &prefered_fmt) -> time_t {
		std::istringstream ss{};

		std::chrono::sys_seconds tp{};
		bool success = false;

		for (size_t i = 0; i < date_formats.size(); ++i) {
			const auto index = (i + prefered_fmt) % date_formats.size();
			const auto &fmt = date_formats.at(index);
			ss.clear();
			ss.str(str);
			
			ss >> std::chrono::parse(fmt, tp);

			if (!ss.fail()) {
				success = true;
				prefered_fmt = index;
				break;
			}
		}

		if (!success) {
			prefered_fmt = 0;
			throw std::runtime_error(fmt::format("Failed to parse date: \"{}\"", str));
		}

		return std::chrono::system_clock::to_time_t(tp);
	}

	auto trim(std::string_view str) -> std::string_view {
		const auto pos1 = str.find_first_not_of(" \t\n\r");
		if (pos1 == std::string::npos) {
			return "";
		}

		const auto pos2 = str.find_last_not_of(" \t\n\r");
		return str.substr(pos1, pos2 - pos1 + 1);
	}

	auto stripUnit(std::string_view header) -> std::pair<std::string, std::string> {
		const auto pos = header.find_last_of('(');
		if (pos == std::string::npos) {
			return {std::string(header), ""};
		}

		const auto name = header.substr(0, pos);
		const auto unit = header.substr(pos + 1, header.find_last_of(')') - pos - 1);

		if (unit.size() > 5) {
			return {std::string(header), ""};
		}

		return {std::string(trim(name)), std::string(trim(unit))};
	}

	auto loadCSV(const std::filesystem::path &path, const std::atomic<bool> &stop_loading)
		-> std::unordered_map<std::string, immediate_dict> {
		using namespace csv;

		std::vector<std::string> col_names{};
		std::unordered_map<std::string, immediate_dict> values{};

		CSVReader reader(path.string());

		for (const auto &header_string : reader.get_col_names() | std::ranges::views::drop(1)) {
			if (header_string.empty()) {
				continue;
			}

			const auto [name, unit] = stripUnit(header_string);

			values[header_string] = {.name = name, .unit = unit, .data = {}};
			col_names.push_back(header_string);
		}

		bool line_error_shown{false};
		std::vector<bool> col_error_shown(col_names.size(), false);
		size_t prefered_date_fmt = 0;

		for (size_t line = 0; auto &row : reader) {
			try {
				const auto date_str = row[0].get<std::string>();
				const auto date = parseDate(date_str, prefered_date_fmt);

				for (size_t col = 0; const auto &col_name : col_names) {
					try {
						const auto val = row[col_name].get<std::string>();
						double dbl_val{std::numeric_limits<double>::quiet_NaN()};

						static constexpr fast_float::parse_options options{fast_float::chars_format::general, ','};
						const auto result =
							fast_float::from_chars_advanced(val.data(), val.data() + val.size(), dbl_val, options);
						if (result.ec == std::errc() && std::isfinite(dbl_val)) {
							values[col_name].data.emplace_back(date, dbl_val);
						}
					} catch (const std::exception & e) {
						if (!col_error_shown[col]) {
							spdlog::warn("Error parsing column {} in file {}:{}: {}", col + 2, path.filename().string(),
										 line + 1, e.what());
							col_error_shown[col] = true;
						}
					}

					if (stop_loading) {
						break;
					}

					++col;
				}
			} catch (const std::exception &e) {
				if (!line_error_shown) {
					spdlog::warn("Error parsing line {}:{}: {}", path.filename().string(), line + 1, e.what());
					line_error_shown = true;
				}
			}

			if (stop_loading) {
				break;
			}

			++line;
		}

		return values;
	}

	template <typename T>
	auto calculateMedian(std::vector<T> data) -> T {
		if (data.empty()) {
			return 0;
		}

		const auto n = data.size() / 2;
		std::nth_element(std::execution::par_unseq, data.begin(), data.begin() + static_cast<long>(n), data.end());

		if (n % 2 != 0) {
			return data.at(n);
		}

		const auto val1 = data.at(n);
		const auto val2 =
			*std::max_element(std::execution::par_unseq, data.cbegin(), data.cbegin() + static_cast<long>(n));

		return (val1 + val2) / T{2};
	}
}  // namespace

auto preparePaths(std::vector<std::filesystem::path> paths) -> std::vector<std::filesystem::path> {
	std::vector<std::filesystem::path> files{};
	files.reserve(paths.size());

	std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) {
		return a.filename() < b.filename();
	});

	for (auto& path : paths) {
		if (std::filesystem::is_directory(path)) {
			for (const auto& entry : std::filesystem::directory_iterator(path)) {
				if (entry.path().extension() == ".csv") {
					files.push_back(entry.path());
				}
			}
		} else {
			files.push_back(path);
		}
	}

	return files;
}

auto loadCSVs(const std::vector<std::filesystem::path> &paths, size_t &finished, const bool &stop_loading)
	-> std::vector<data_dict_t> {
	if (paths.empty()) {
		return {};
	}

	std::unordered_map<std::string, immediate_dict> values_temp{};
	std::vector<data_dict_t> values{};

	struct context {
		size_t index;
		std::filesystem::path path;
		std::unordered_map<std::string, immediate_dict> values{};
	};

	std::vector<context> contexts{};
	contexts.reserve(paths.size());

	for (size_t i = 0; const auto &path : paths) {
		contexts.push_back({.index = ++i, .path = path});
	}

	auto fn = [&contexts, &stop_loading, &finished](auto &ctx) {
		if (!stop_loading) {
			spdlog::info("Loading file: {} ({}/{})...", ctx.path.filename().string(), ctx.index, contexts.size());
			try {
				ctx.values = loadCSV(ctx.path, stop_loading);
			} catch (const std::exception &e) {
				spdlog::error("{}", e.what());
			}
			++finished;
		}
	};

	std::for_each(std::execution::seq, contexts.begin(), contexts.end(), fn);

	if (stop_loading) {
		return {};
	}

	spdlog::debug("Merging data...");

	for (const auto &ctx : contexts) {
		for (const auto &[key, value] : ctx.values) {
			if (value.data.empty()) {
				continue;
			}

			if (values_temp.find(key) == values_temp.end()) {
				values_temp[key] = value;
			} else {
				values_temp[key].data.insert(values_temp[key].data.end(), value.data.begin(), value.data.end());
			}
		}
	}

	values.reserve(values_temp.size());

	for (auto &&[key, value] : values_temp) {
		std::sort(std::execution::par, value.data.begin(), value.data.end(),
				  [](const auto &a, const auto &b) { return a.first < b.first; });

		data_dict_t dd{};
		dd.name = value.name;
		dd.uuid = uuids::to_string(UUIDGenerator::getInstance().generate());
		dd.unit = value.unit;

		bool is_boolean = true;

		for (const auto &[date, val] : value.data) {
			dd.timestamp->push_back(date);
			dd.data->push_back(val);

			if (val != 0 && val != 1) {
				is_boolean = false;
			}
		}

		dd.data_type = is_boolean ? data_type_t::BOOLEAN : data_type_t::FLOAT;

		std::vector<time_t> time_deltas{};
		time_deltas.reserve(dd.timestamp->size() - 1);

		for (size_t i = 1; i < dd.timestamp->size(); ++i) {
			time_deltas.push_back(dd.timestamp->at(i) - dd.timestamp->at(i - 1));
		}

		dd.delta_t = calculateMedian(time_deltas);

		values.push_back(dd);
	}

	return values;
}