#include "csv_handling.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <ctime>
#ifndef __APPLE__
#include <execution>
#endif
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "date/date.h"
#include "csv.hpp"
#include "dicts.hpp"
#include "fast_float/fast_float.h"
#include "spdlog/spdlog.h"
#include "string_helpers.hpp"
#include "utility.hpp"
#include "uuid_generator.hpp"

namespace {
	constexpr auto default_date_formats = std::array{
		"%Y/%m/%d %H:%M:%S%Ez",
		"%Y/%m/%d %H:%M:%S%z",
		"%Y-%m-%d %H:%M:%S%Ez",
		"%Y-%m-%d %H:%M:%S%z",
		"%Y-%m-%dT%H:%M:%S%Ez",
		"%Y-%m-%dT%H:%M:%S%z",
		"%d.%m.%Y %H:%M:%S%Ez",
		"%d.%m.%Y %H:%M:%S%z",
		"%d/%m/%Y %H:%M:%S%Ez",
		"%d/%m/%Y %H:%M:%S%z",
		"%m/%d/%Y %H:%M:%S%Ez",
		"%m/%d/%Y %H:%M:%S%z",
		"%Y/%m/%d %H:%M:%S",
		"%Y-%m-%d %H:%M:%S",
		"%Y-%m-%dT%H:%M:%S",
		"%d.%m.%Y %H:%M:%S",
		"%d/%m/%Y %H:%M:%S",
		"%m/%d/%Y %H:%M:%S"
	};

	auto formatHasTimezone(const std::string_view format) -> bool {
		for (size_t i = 0; i < format.size(); ++i) {
			if (format[i] != '%') {
				continue;
			}

			if (i + 1 >= format.size()) {
				break;
			}

			const auto spec = format[i + 1];
			if (spec == '%') {
				++i;
				continue;
			}

			if (spec == 'z' || spec == 'Z') {
				return true;
			}

			if ((spec == 'E' || spec == 'O') && i + 2 < format.size()) {
				const auto ext_spec = format[i + 2];
				if (ext_spec == 'z' || ext_spec == 'Z') {
					return true;
				}
			}
		}

		return false;
	}

	auto parseDateTimeWithFormat(const std::string_view value, const std::string_view format, time_t* out_time) -> bool {
		std::istringstream ss{std::string{value}};
		const auto format_c_str = std::string{format};

		if (formatHasTimezone(format)) {
			std::chrono::sys_seconds tp{};
			std::string abbrev{};
			std::chrono::minutes offset{};
			ss >> date::parse(format_c_str.c_str(), tp, abbrev, offset);
			if (ss.fail()) {
				return false;
			}

			ss >> std::ws;
			if (!ss.eof()) {
				return false;
			}

			if (out_time != nullptr) {
				*out_time = std::chrono::system_clock::to_time_t(tp);
			}

			return true;
		}

		std::tm tm{};
		tm.tm_isdst = -1;
		ss >> std::get_time(&tm, format_c_str.c_str());
		if (ss.fail()) {
			return false;
		}

		ss >> std::ws;
		if (!ss.eof()) {
			return false;
		}

		if (out_time != nullptr) {
			*out_time = std::mktime(&tm);
		}

		return true;
	}

	auto buildFormatList(const csv_parse_config_t& config) -> std::vector<std::string_view> {
		if (!config.date_format.empty()) {
			return {std::string_view{config.date_format}};
		}
		return {default_date_formats.begin(), default_date_formats.end()};
	}

	auto splitLine(std::string_view line, const char delimiter) -> std::vector<std::string_view> {
		std::vector<std::string_view> parts;
		size_t start = 0;

		while (start <= line.size()) {
			const auto pos = line.find(delimiter, start);
			if (pos == std::string_view::npos) {
				parts.push_back(trim(line.substr(start)));
				break;
			}

			parts.push_back(trim(line.substr(start, pos - start)));
			start = pos + 1;
		}

		return parts;
	}

	auto readSampleLines(const std::filesystem::path& path, const size_t max_lines) -> std::vector<std::string> {
		std::ifstream input(path);
		if (!input) {
			return {};
		}

		std::vector<std::string> lines;
		lines.reserve(max_lines);

		std::string line;
		while (lines.size() < max_lines && std::getline(input, line)) {
			if (!trim(line).empty()) {
				lines.push_back(line);
			}
		}

		return lines;
	}

	auto tryParseDateWithFormat(const std::string_view value, const std::string_view format) -> bool {
		return parseDateTimeWithFormat(value, format, nullptr);
	}

	auto detectDecimalSeparator(const std::vector<std::string>& lines, const char delimiter,
							const size_t date_column_index, const char fallback) -> char {
		size_t comma_hits = 0;
		size_t period_hits = 0;

		for (size_t i = 1; i < lines.size(); ++i) {
			const auto columns = splitLine(lines[i], delimiter);
			for (size_t col = 0; col < columns.size(); ++col) {
				if (col == date_column_index) {
					continue;
				}

				const auto token = columns[col];
				if (token.size() < 3) {
					continue;
				}

				for (size_t idx = 1; idx + 1 < token.size(); ++idx) {
					if (!std::isdigit(static_cast<unsigned char>(token[idx - 1])) ||
						!std::isdigit(static_cast<unsigned char>(token[idx + 1]))) {
						continue;
					}

					if (token[idx] == ',') {
						++comma_hits;
					} else if (token[idx] == '.') {
						++period_hits;
					}
				}
			}
		}

		if (comma_hits == period_hits) {
			return fallback;
		}

		return (comma_hits > period_hits) ? ',' : '.';
	}

	struct inferred_config_t {
		char field_delimiter;
		size_t date_column_index;
		std::string_view date_format;
		size_t date_score;
		size_t checked_rows;
	};

	auto inferConfigFromLines(const std::vector<std::string>& lines, const csv_parse_config_t& current_config)
		-> std::optional<csv_parse_config_t> {
		if (lines.size() < 2) {
			return std::nullopt;
		}

		constexpr std::array<char, 4> delimiter_candidates{',', ';', '\t', '|'};

		std::optional<inferred_config_t> best{};

		for (const auto delimiter : delimiter_candidates) {
			const auto header_cols = splitLine(lines.front(), delimiter);
			if (header_cols.size() < 2) {
				continue;
			}

			const auto max_columns = header_cols.size();
			const size_t rows_to_check = std::min<size_t>(lines.size() - 1, 12);

			for (size_t col = 0; col < max_columns; ++col) {
				for (const auto* const format : default_date_formats) {
					size_t date_score = 0;

					for (size_t line_idx = 1; line_idx <= rows_to_check; ++line_idx) {
						const auto cols = splitLine(lines[line_idx], delimiter);
						if (col >= cols.size()) {
							continue;
						}

						if (tryParseDateWithFormat(cols[col], format)) {
							++date_score;
						}
					}

					if (!best.has_value() || date_score > best->date_score) {
						best = inferred_config_t{
							.field_delimiter = delimiter,
							.date_column_index = col,
							.date_format = format,
							.date_score = date_score,
							.checked_rows = rows_to_check
						};
					}
				}
			}
		}

		if (!best.has_value() || best->date_score == 0 || best->checked_rows == 0) {
			return std::nullopt;
		}

		const size_t min_required = std::min<size_t>(3, best->checked_rows);
		if (best->date_score < min_required) {
			return std::nullopt;
		}

		auto detected = current_config;
		detected.field_delimiter = best->field_delimiter;
		detected.date_column_index = best->date_column_index;
		detected.date_format = std::string{best->date_format};
		detected.decimal_separator = detectDecimalSeparator(lines, detected.field_delimiter,
											  detected.date_column_index, current_config.decimal_separator);

		return detected;
	}

	auto parseDate(const std::string& str, size_t& prefered_fmt, std::span<const std::string_view> formats) -> time_t {
		bool success = false;
		time_t parsed_time{};

		for (size_t i = 0; i < formats.size(); ++i) {
			const auto index = (i + prefered_fmt) % formats.size();
			const auto& fmt = formats[index];

			if (parseDateTimeWithFormat(str, fmt, &parsed_time)) {
				success = true;
				prefered_fmt = index;
				break;
			}
		}

		if (!success) {
			prefered_fmt = 0;
			throw std::runtime_error(fmt::format("Failed to parse date: \"{}\"", str));
		}

		return parsed_time;
	}

	auto loadCSV(const std::filesystem::path &path, const std::atomic<bool> &stop_loading,
	             const csv_parse_config_t &config, std::string &parse_error_sample)
		-> std::unordered_map<std::string, immediate_dict> {
		using namespace csv;

		CSVFormat format;
		format.delimiter(config.field_delimiter);
		if (config.first_row_is_header) {
			format.header_row(0);
		} else {
			format.no_header();
		}
		format.column_names_policy(ColumnNamePolicy::CASE_INSENSITIVE);

		std::vector<std::string> col_names{};
		std::unordered_map<std::string, immediate_dict> values{};

		CSVReader reader(path.string(), format);

		std::vector<std::string> all_col_names{};
		if (config.first_row_is_header) {
			all_col_names = reader.get_col_names();
		} else {
			const auto sample_lines = readSampleLines(path, 1);
			const auto column_count = sample_lines.empty() ? 0uz : splitLine(sample_lines.front(), config.field_delimiter).size();
			all_col_names.reserve(column_count);
			for (size_t i = 0; i < column_count; ++i) {
				all_col_names.push_back(fmt::format("col {}", i));
			}
		}

		const auto date_col_idx = all_col_names.empty() ? 0uz : std::min(config.date_column_index, all_col_names.size() - 1uz);

		for (size_t i = 0; i < all_col_names.size(); ++i) {
			if (i == date_col_idx) {
				continue;
			}
			const auto &header_string = all_col_names[i];
			if (header_string.empty()) {
				continue;
			}

			const auto [name, unit] = config.first_row_is_header ? stripUnit(header_string) : std::pair{header_string, std::string{}};

			values[header_string] = {.name = name, .unit = unit, .data = {}};
			col_names.push_back(header_string);
		}

		if (col_names.empty()) {
			parse_error_sample = "(no data columns found)";
			return {};
		}

		const auto fmt_list = buildFormatList(config);
		const fast_float::parse_options parse_opts{fast_float::chars_format::general, config.decimal_separator};

		bool line_error_shown{false};
		std::vector<bool> col_error_shown(col_names.size(), false);
		size_t prefered_date_fmt = 0;

		for (size_t line = 0; auto &row : reader) {
			try {
				const auto date_str = row[date_col_idx].get<std::string>();
				const auto date = parseDate(date_str, prefered_date_fmt, fmt_list);

				for (size_t col = 0; const auto &col_name : col_names) {
					try {
						const auto source_col_idx = col >= date_col_idx ? col + 1 : col;
						const auto val = config.first_row_is_header ? row[col_name].get<std::string>() : row[source_col_idx].get<std::string>();
						double dbl_val{std::numeric_limits<double>::quiet_NaN()};

						const auto result =
							fast_float::from_chars_advanced(val.data(), val.data() + val.size(), dbl_val, parse_opts);
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
					if (parse_error_sample.empty()) {
						try { parse_error_sample = row[0].get<std::string>(); } catch (...) {}
					}
				}
			}

			if (stop_loading) {
				break;
			}

			++line;
		}

		const auto any_data = std::ranges::any_of(values, [](const auto &p) { return !p.second.data.empty(); });
		if (!any_data && parse_error_sample.empty()) {
			parse_error_sample = "(no data could be parsed)";
		}

		return values;
	}

	template <typename T>
	auto calculateMedian(std::vector<T> data) -> T {
		if (data.empty()) {
			return 0;
		}

		const auto n = data.size() / 2;
#ifdef __APPLE__
		std::nth_element(data.begin(), data.begin() + static_cast<long>(n), data.end());
#else
		std::nth_element(std::execution::par_unseq, data.begin(), data.begin() + static_cast<long>(n), data.end());
#endif

		if (data.size() % 2 != 0) {
			return data.at(n);
		}

		const auto val1 = data.at(n);
#ifdef __APPLE__
		const auto val2 =
			*std::max_element(data.cbegin(), data.cbegin() + static_cast<long>(n));
#else
		const auto val2 =
			*std::max_element(std::execution::par_unseq, data.cbegin(), data.cbegin() + static_cast<long>(n));
#endif

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

auto loadCSVs(const std::vector<std::filesystem::path>& paths, size_t& finished, const std::atomic<bool>& stop_loading,
			  const csv_parse_config_t& config, std::string& parse_error_out) -> std::vector<data_dict_t> {
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

	auto fn = [&contexts, &stop_loading, &finished, &config, &parse_error_out](auto &ctx) {
		if (!stop_loading) {
			spdlog::info("Loading file: {} ({}/{})...", ctx.path.filename().string(), ctx.index, contexts.size());
			try {
				ctx.values = loadCSV(ctx.path, stop_loading, config, parse_error_out);
			} catch (const std::exception &e) {
				spdlog::error("{}", e.what());
			}
			++finished;
		}
	};

#ifdef __APPLE__
	std::for_each(contexts.begin(), contexts.end(), fn);
#else
	std::for_each(std::execution::seq, contexts.begin(), contexts.end(), fn);
#endif

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
#ifdef __APPLE__
		std::sort(value.data.begin(), value.data.end(),
				  [](const auto &a, const auto &b) { return a.first < b.first; });
#else
		std::sort(std::execution::par, value.data.begin(), value.data.end(),
				  [](const auto &a, const auto &b) { return a.first < b.first; });
#endif

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

auto inferCSVParseConfig(const std::vector<std::filesystem::path>& paths,
						 const csv_parse_config_t& current_config) -> std::optional<csv_parse_config_t> {
	if (paths.empty()) {
		return std::nullopt;
	}

	const auto sample_lines = readSampleLines(paths.front(), 16);
	return inferConfigFromLines(sample_lines, current_config);
}