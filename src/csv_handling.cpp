#include "csv_handling.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <execution>
#include <filesystem>
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
#include "spdlog/spdlog.h"
#include "utility.hpp"
#include "uuid_generator.hpp"

namespace {
	auto parseDate(const std::string &str) -> time_t {
		static const auto locale = std::locale("de_DE.utf-8");
		std::istringstream ss(str);
		ss.imbue(locale);

		std::chrono::sys_seconds tp{};
		ss >> std::chrono::parse("%Y/%m/%d %H:%M:%S", tp);

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

		for (auto &row : reader) {
			const auto date_str = row[0].get<std::string>();
			const auto date = parseDate(date_str);

			for (const auto &col_name : col_names) {
				try {
					auto val = row[col_name].get<std::string>();

					const auto pos = val.find(',');
					if (pos != std::string::npos) {
						val = val.replace(pos, 1, ".");
					}

					const auto dbl_val = std::stod(val);

					if (std::isfinite(dbl_val)) {
						values[col_name].data.push_back({date, dbl_val});
					}
				} catch (const std::exception & /*e*/) {}

				if (stop_loading) {
					break;
				}
			}

			if (stop_loading) {
				break;
			}
		}

		return values;
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

auto loadCSVs(const std::vector<std::filesystem::path> &paths, std::atomic<size_t> &finished,
			  std::atomic<bool> &stop_loading, bool parallel_loading) -> std::vector<data_dict_t> {
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

	if (parallel_loading) {
		std::for_each(std::execution::par, contexts.begin(), contexts.end(), fn);
	} else {
		std::for_each(std::execution::seq, contexts.begin(), contexts.end(), fn);
	}

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

		for (const auto &[date, val] : value.data) {
			dd.timestamp.push_back(date);
			dd.data.push_back(val);
		}

		values.push_back(dd);
	}

	return values;
}