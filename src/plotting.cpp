#include "plotting.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <ranges>
#include <utility>

#include "custom_type_traits.hpp"
#include "dicts.hpp"
#include "imgui.h"
#include "implot.h"
#include "implot_internal.h"
#include "spdlog/spdlog.h"
#include "utility.hpp"

namespace {
	struct plot_data_t {
		const data_dict_t *data;
		size_t reduction_factor;
		size_t start_index;
		int count;
	};

	auto plotDict(int i, void *data) -> ImPlotPoint {
		assert(i >= 0);
		assert(data != nullptr);

		const auto &plot_data = *static_cast<plot_data_t *>(data);
		const auto &dd = *plot_data.data;

		if (i == 0) {
			return {static_cast<double>(dd.timestamp.front()), std::numeric_limits<double>::quiet_NaN()};
		}

		if (i == plot_data.count - 1) {
			return {static_cast<double>(dd.timestamp.back()), std::numeric_limits<double>::quiet_NaN()};
		}

		const auto index = plot_data.start_index + (static_cast<size_t>(i - 1) * plot_data.reduction_factor);
		return {static_cast<double>(dd.timestamp[index]), dd.data[index]};
	}

	auto getDateRange(const data_dict_t &data) -> std::pair<time_t, time_t> {
		if (data.timestamp.empty()) {
			return {0, 0};
		}

		return {data.timestamp.front(), data.timestamp.back()};
	}

	auto getIndicesFromTimeRange(const std::vector<time_t> &date, const ImPlotRange &limits)
		-> std::pair<size_t, size_t> {
		const auto start = static_cast<time_t>(limits.Min);
		const auto stop = static_cast<time_t>(limits.Max);
		const auto start_it = std::ranges::lower_bound(date, start);
		const auto stop_it = std::ranges::upper_bound(date, stop);

		return {static_cast<size_t>(start_it - date.begin()), static_cast<size_t>(stop_it - date.begin())};
	}

	auto getXLims(const std::vector<data_dict_t> &data) -> std::pair<double, double> {
		auto date_min = std::numeric_limits<time_t>::max();
		auto date_max = std::numeric_limits<time_t>::lowest();

		for (const auto &col : data) {
			if (!col.visible) {
				continue;
			}

			if (col.timestamp.empty()) {
				continue;
			}

			date_min = std::min(date_min, col.timestamp.front());
			date_max = std::max(date_max, col.timestamp.back());
		}

		return {static_cast<double>(date_min), static_cast<double>(date_max)};
	}

	[[maybe_unused]] auto fixSubplotRanges(const std::vector<data_dict_t> &data) -> void {
		auto *implot_ctx = ImPlot::GetCurrentContext();
		auto *subplot = implot_ctx->CurrentSubplot;

		if (subplot == nullptr) {
			return;
		}

		auto data_min = std::numeric_limits<double>::max();
		auto data_max = std::numeric_limits<double>::lowest();
		const auto [date_min, date_max] = getXLims(data);

		for (const auto &col : data) {
			if (!col.visible) {
				continue;
			}

			if (col.timestamp.empty()) {
				continue;
			}

			data_min = std::min(data_min, *std::ranges::min_element(col.data));
			data_max = std::max(data_max, *std::ranges::max_element(col.data));
		}

		for (auto &col_link_data : subplot->ColLinkData) {
			if (col_link_data.Min == 0 && col_link_data.Max == 1) {
				col_link_data.Min = static_cast<double>(date_min);
				col_link_data.Max = static_cast<double>(date_max);
			}
		}

		for (auto &row_link_data : subplot->RowLinkData) {
			if (row_link_data.Min == 0 && row_link_data.Max == 1) {
				row_link_data.Min = data_min;
				row_link_data.Max = data_max;
			}
		}
	}
}  // namespace

auto plotDataInSubplots(const std::vector<data_dict_t> &data, size_t max_data_points, const std::string &uuid,
						bool global_x_link) -> void {
	auto v_min = ImGui::GetWindowContentRegionMin();
	auto v_max = ImGui::GetWindowContentRegionMax();

	v_min.x += ImGui::GetWindowPos().x;
	v_min.y += ImGui::GetWindowPos().y;
	v_max.x += ImGui::GetWindowPos().x;
	v_max.y += ImGui::GetWindowPos().y;

	const auto plot_height = v_max.y - v_min.y;
	const auto plot_width = v_max.x - v_min.x;

	max_data_points = std::max(max_data_points, 1uz);

	const auto n_selected = std::count_if(data.begin(), data.end(), [](const auto &dct) { return dct.visible; });

	if (n_selected == 0) {
		return;
	}

	const auto [rows, cols] = [n_selected]() -> std::pair<int, int> {
		if (n_selected <= 3) {
			return {n_selected, 1};
		}

		return {(n_selected + 1) / 2, 2};
	}();

	const auto subplot_id = "##" + uuid;
	const auto subplot_flags =
		(n_selected > 1 ? ImPlotSubplotFlags_ShareItems : 0) | (!global_x_link ? ImPlotSubplotFlags_LinkAllX : 0);

	static auto [global_link_min, global_link_max] = getXLims(data);

	if (ImPlot::BeginSubplots(subplot_id.c_str(), rows, cols, ImVec2(plot_width, plot_height), subplot_flags)) {
		if (!global_x_link) {
			fixSubplotRanges(data);
		}

		for (const auto &col : data) {
			if (!col.visible) {
				continue;
			}

			if (global_x_link) {
				ImPlot::SetNextAxisLinks(ImAxis_X1, &global_link_min, &global_link_max);
			}

			const auto plot_title = col.name + "##" + col.uuid;
			if (ImPlot::BeginPlot(plot_title.c_str(), ImVec2(plot_width, plot_height),
								  (n_selected < 1 ? ImPlotFlags_NoLegend : 0) | ImPlotFlags_NoTitle)) {
				const auto fmt = [&col]() -> std::string {
					if (col.unit.empty()) {
						return "%g";
					}

					if (col.unit == "%") {
						return "%g%%";
					}

					return "%g " + col.unit;
				}();

				ImPlot::SetupAxes("date", col.name.c_str());
				ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
				ImPlot::SetupAxisFormat(ImAxis_Y1, fmt.c_str());

				const auto date_range = getDateRange(col);
				ImPlot::SetupAxisLimits(ImAxis_X1, static_cast<double>(date_range.first),
										static_cast<double>(date_range.second), ImGuiCond_Once);

				const auto limits = ImPlot::GetPlotLimits(ImAxis_X1);
				const auto [start_index, stop_index] = getIndicesFromTimeRange(col.timestamp, limits.X);
				const auto points_in_range = stop_index - start_index;
				const auto reduction_factor = std::clamp(fastCeil<size_t>(points_in_range, max_data_points), 1uz,
														 std::numeric_limits<size_t>::max());

				const auto count = coerceCast<int>(fastCeil(points_in_range, reduction_factor)) + 2;
				plot_data_t plot_data{
					.data = &col, .reduction_factor = reduction_factor, .start_index = start_index, .count = count};

				ImPlot::PlotLineG(col.name.c_str(), plotDict, &plot_data, count);
				ImPlot::EndPlot();
			}
		}

		ImPlot::EndSubplots();
	}
}
