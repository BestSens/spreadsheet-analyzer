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

	auto getDateRange(const data_dict_t &data) -> std::pair<double, double> {
		if (data.timestamp.empty()) {
			return {0, 0};
		}

		const auto date_min = static_cast<double>(data.timestamp.front());
		const auto date_max = static_cast<double>(data.timestamp.back());

		const auto padding_percent = ImPlot::GetStyle().FitPadding.x;
		const auto full_range = date_max - date_min;
		const auto padding = full_range * static_cast<double>(padding_percent);
		return {date_min - padding, date_max + padding};
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

	auto getPaddedXLims(const std::vector<data_dict_t> &data) -> std::pair<double, double> {
		auto [date_min, date_max] = getXLims(data);

		const auto padding_percent = ImPlot::GetStyle().FitPadding.x;
		const auto full_range = date_max - date_min;
		const auto padding = full_range * static_cast<double>(padding_percent);

		return {date_min - padding, date_max + padding};
	}

	[[maybe_unused]] auto fixSubplotRanges(const std::vector<data_dict_t> &data) -> void {
		auto *implot_ctx = ImPlot::GetCurrentContext();
		auto *subplot = implot_ctx->CurrentSubplot;

		if (subplot == nullptr) {
			return;
		}

		auto data_min = std::numeric_limits<double>::max();
		auto data_max = std::numeric_limits<double>::lowest();
		const auto [date_min, date_max] = getPaddedXLims(data);

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
				col_link_data.Min = date_min;
				col_link_data.Max = date_max;
			}
		}

		for (auto &row_link_data : subplot->RowLinkData) {
			if (row_link_data.Min == 0 && row_link_data.Max == 1) {
				const auto padding_percent = ImPlot::GetStyle().FitPadding.y;
				const auto full_range = data_max - data_min;
				const auto padding = full_range * static_cast<double>(padding_percent);
				
				row_link_data.Min = data_min - padding;
				row_link_data.Max = data_max + padding;
			}
		}
	}

	auto getPlotSize() -> ImVec2 {
		auto v_min = ImGui::GetWindowContentRegionMin();
		auto v_max = ImGui::GetWindowContentRegionMax();

		v_min.x += ImGui::GetWindowPos().x;
		v_min.y += ImGui::GetWindowPos().y;
		v_max.x += ImGui::GetWindowPos().x;
		v_max.y += ImGui::GetWindowPos().y;

		const auto plot_height = v_max.y - v_min.y - 5.0f;
		const auto plot_width = v_max.x - v_min.x;

		return {plot_width, plot_height};
	}
}  // namespace

auto plotDataInSubplots(const std::vector<data_dict_t> &data, size_t max_data_points, const std::string &uuid,
						bool global_x_link) -> void {
	const auto plot_size = getPlotSize();

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

	static auto [global_link_min, global_link_max] = getPaddedXLims(data);

	if (ImPlot::BeginSubplots(subplot_id.c_str(), rows, cols, plot_size, subplot_flags)) {
		if (!global_x_link) {
			fixSubplotRanges(data);
		}

		const auto* current_subplot = ImPlot::GetCurrentContext()->CurrentSubplot;
		const auto current_flags = current_subplot->Flags;
		const auto is_x_linked = global_x_link || (current_flags & ImPlotSubplotFlags_LinkAllX) != 0 ||
								 (current_flags & ImPlotSubplotFlags_LinkCols) != 0;

		for (int i = 0; const auto &col : data) {
			if (!col.visible) {
				continue;
			}

			if (global_x_link) {
				ImPlot::SetNextAxisLinks(ImAxis_X1, &global_link_min, &global_link_max);
			}

			const auto plot_title = col.name + "##" + col.uuid;
			if (ImPlot::BeginPlot(plot_title.c_str(), ImVec2(-1, 0),
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

				const auto show_x_axis = [&]() -> bool {
					if (i == n_selected - 1) {
						return true;
					}

					return (!is_x_linked && !global_x_link);
				}();
				++i;

				const auto x_axis_flags = (!show_x_axis ? ImPlotAxisFlags_NoTickLabels : 0) | ImPlotAxisFlags_NoLabel;

				ImPlot::SetupAxes("date", col.name.c_str(), x_axis_flags);
				ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
				ImPlot::SetupAxisFormat(ImAxis_Y1, fmt.c_str());

				const auto date_range = [is_x_linked, global_x_link, current_subplot, &col]() {
					if (is_x_linked) {
						if (global_x_link) {
							return std::pair{global_link_min, global_link_max};
						}

						const auto min = current_subplot->ColLinkData[0].Min;
						const auto max = current_subplot->ColLinkData[0].Max;

						return std::pair{min, max};
					}

					return getDateRange(col);
				}();
				ImPlot::SetupAxisLimits(ImAxis_X1, date_range.first, date_range.second, ImGuiCond_Once);

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
