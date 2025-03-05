#include "plotting.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <numeric>
#include <ranges>
#include <utility>

#include "custom_type_traits.hpp"
#include "dicts.hpp"
#include "global_state.hpp"
#include "imgui.h"
#include "imgui_extensions.hpp"
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

		std::pair<double, double> linked_date_range;
	};

	auto calcMax(std::span<const double> data) -> double {
		return *std::ranges::max_element(data);
	}

	auto calcMin(std::span<const double> data) -> double {
		return *std::ranges::min_element(data);
	}

	auto calcMean(std::span<const double> data) -> double {
		return std::accumulate(data.begin(), data.end(), 0.0) / static_cast<double>(data.size());
	}

	auto calcStd(std::span<const double> data) -> double {
		const auto mean = calcMean(data);
		return std::sqrt(std::accumulate(data.begin(), data.end(), 0.0,
										 [mean](const auto &a, const auto &b) { return a + (b - mean) * (b - mean); }) /
						 static_cast<double>(data.size()));
	}

	auto getAggregatedPlotData(int i, void *data, auto fn) -> ImPlotPoint {
		assert(i >= 0);
		assert(data != nullptr);

		const auto &plot_data = *static_cast<plot_data_t *>(data);
		const auto &dd = *plot_data.data;

		assert(dd.aggregated_to > 0);
		assert(dd.aggregated_to == plot_data.reduction_factor);

		if (i == 0) {
			return {plot_data.linked_date_range.first, std::numeric_limits<double>::quiet_NaN()};
		}

		if (i == plot_data.count - 1) {
			return {plot_data.linked_date_range.second, std::numeric_limits<double>::quiet_NaN()};
		}

		const auto resulting_idx =
			std::min(plot_data.start_index + coerceCast<size_t>(i) - 1, dd.aggregates.size() - 1);

		try {
			const auto &aggregate = dd.aggregates.at(resulting_idx);
			return {static_cast<double>(aggregate.date), fn(aggregate)};
		} catch (const std::exception &e) {
			spdlog::error("{} i = {}, plot_data.start_index = {}, aggregates.size() = {}", e.what(), resulting_idx,
						  plot_data.start_index, dd.aggregates.size());
		}

		return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
	}

	auto plotDict(int i, void *data) -> ImPlotPoint {
		return getAggregatedPlotData(i, data, [](const auto &aggregate) { return aggregate.first; });
	}

	auto plotDictMean(int i, void *data) -> ImPlotPoint {
		return getAggregatedPlotData(i, data, [](const auto &aggregate) { return aggregate.mean; });
	}

	auto plotDictMax(int i, void *data) -> ImPlotPoint {
		return getAggregatedPlotData(i, data, [](const auto &aggregate) { return aggregate.max; });
	}

	auto plotDictMin(int i, void *data) -> ImPlotPoint {
		return getAggregatedPlotData(i, data, [](const auto &aggregate) { return aggregate.min; });
	}

	auto plotDictStdPlus(int i, void *data) -> ImPlotPoint {
		return getAggregatedPlotData(i, data, [](const auto &aggregate) { return aggregate.mean + aggregate.std; });
	}

	auto plotDictStdMinus(int i, void *data) -> ImPlotPoint {
		return getAggregatedPlotData(i, data, [](const auto &aggregate) { return aggregate.mean - aggregate.std; });
	}

	auto checkAggregate(data_dict_t& dict, size_t reduction_factor) -> void {
		if (dict.aggregated_to == reduction_factor && !dict.aggregates.empty()) {
			return;
		}

		dict.aggregates.clear();
		dict.aggregates.reserve((dict.data.size() / reduction_factor) + 1);

		spdlog::debug("recalculating aggregates for {} with reduction factor {}", dict.name, reduction_factor);

		for (size_t i = 0; i < dict.data.size() - reduction_factor; i += reduction_factor) {
			const auto count = std::min(reduction_factor, dict.data.size() - i);
			const auto mean = calcMean(std::span{dict.data}.subspan(i, count));
			const auto stdev = calcStd(std::span{dict.data}.subspan(i, count));
			const auto min = calcMin(std::span{dict.data}.subspan(i, count));
			const auto max = calcMax(std::span{dict.data}.subspan(i, count));

			dict.aggregates.push_back({.date = dict.timestamp[i],
										.min = min,
										.max = max,
										.mean = mean,
										.std = stdev,
										.first = dict.data[i]});
		}

		dict.aggregated_to = reduction_factor;

		spdlog::debug("recalculated aggregates for {} with reduction factor {}", dict.name, reduction_factor);
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

		auto start_index = static_cast<size_t>(start_it - date.begin());
		auto stop_index = static_cast<size_t>(stop_it - date.begin());

		if (start_index > 0) {
			start_index -= 1;
		}

		stop_index = std::min(stop_index, date.size() - 1);

		return {start_index, stop_index};
	}

	auto getIndicesFromAggregate(const std::vector<data_aggregate_t> &agg, const ImPlotRange &limits)
		-> std::pair<size_t, size_t> {
		const auto start = static_cast<time_t>(limits.Min);
		const auto stop = static_cast<time_t>(limits.Max);
		const auto start_it =
			std::ranges::lower_bound(agg, start, std::ranges::less{}, &data_aggregate_t::date);
		const auto stop_it =
			std::ranges::upper_bound(agg, stop, std::ranges::less{}, &data_aggregate_t::date);

		auto start_index = static_cast<size_t>(start_it - agg.begin());
		auto stop_index = static_cast<size_t>(stop_it - agg.begin());

		if (start_index > 0) {
			start_index -= 1;
		}

		stop_index = std::min(stop_index, agg.size() - 1);

		return {start_index, stop_index};
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

	auto getCursorColor() -> ImVec4 {
		auto temp = ImGui::GetStyle().Colors[ImGuiCol_Text];
		temp.w = 0.25f;

		return temp;
	}

	auto doPlot(int current_pos, int n_selected, int col_count, data_dict_t &col, const ImVec4 &plot_color,
				const std::pair<double, double> &window_date_range) -> void {
		auto &app_state = AppState::getInstance();
		const auto max_data_points = static_cast<size_t>(std::max(app_state.max_data_points, 1));
		const auto global_x_link = app_state.global_x_link;
		double &global_link_min = app_state.global_link.first;
		double &global_link_max = app_state.global_link.second;

		const auto *current_subplot = ImPlot::GetCurrentContext()->CurrentSubplot;
		const auto current_flags = current_subplot->Flags;
		const auto is_x_linked = global_x_link || (current_flags & ImPlotSubplotFlags_LinkAllX) != 0 ||
								 (current_flags & ImPlotSubplotFlags_LinkCols) != 0;

		if (global_x_link) {
			ImPlot::SetNextAxisLinks(ImAxis_X1, &global_link_min, &global_link_max);
		}

		const auto cursor_color = getCursorColor();

		const auto plot_title = col.name + "##" + col.uuid;
		const auto inf_line_name = "##" + col.uuid + "inf_line";
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
				if (current_pos >= n_selected - col_count) {
					return true;
				}

				return (!is_x_linked && !global_x_link);
			}();

			const auto x_axis_flags = (!show_x_axis ? ImPlotAxisFlags_NoTickLabels : 0) | ImPlotAxisFlags_NoLabel;

			ImPlot::SetupAxes("date", col.name.c_str(), x_axis_flags);
			ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
			ImPlot::SetupAxisFormat(ImAxis_Y1, fmt.c_str());

			const auto date_range = [is_x_linked, global_x_link, current_subplot, &col, &global_link_min,
									 &global_link_max]() {
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
			const auto reduction_factor =
				std::clamp(fastCeil<size_t>(points_in_range, max_data_points), 1uz, std::numeric_limits<size_t>::max());
			const auto reduction_factor_stepped = std::bit_ceil(reduction_factor);

			const auto date_lims = [&]() {
				if (is_x_linked) {
					if (global_x_link) {
						return app_state.date_range;
					}

					return window_date_range;
				}

				return getDateRange(col);
			}();

			checkAggregate(col, reduction_factor_stepped);

			const auto [start_index_agg, stop_index_agg] = getIndicesFromAggregate(col.aggregates, limits.X);
			const auto count = [&]() -> int {
				auto count = stop_index_agg - start_index_agg + 1;
				count = std::clamp(count, 0uz, col.aggregates.size());
				return static_cast<int>(count);
			}();
			const auto padded_count = count + 2;

			plot_data_t plot_data{.data = &col,
								  .reduction_factor = reduction_factor_stepped,
								  .start_index = start_index_agg,
								  .count = padded_count,
								  .linked_date_range = date_lims};

			if (ImPlot::IsPlotHovered()) {
				app_state.global_x_mouse_position =
					ImPlot::GetCurrentPlot()->XAxis(0).PixelsToPlot(ImGui::GetIO().MousePos.x);
			}
			
			switch (col.data_type) {
				using enum data_type_t;
				case BOOLEAN:
					ImPlot::SetNextFillStyle(plot_color, 0.8f);
					ImPlot::PlotDigitalG(col.name.c_str(), plotDict, &plot_data, padded_count);
					break;
				default:
					ImPlot::SetNextLineStyle(plot_color);

					if (reduction_factor > 1) {
						const auto shaded_name = "##" + col.name + "##shaded";
						ImPlot::PlotLineG(col.name.c_str(), plotDictMean, &plot_data, padded_count);
						ImPlot::SetNextFillStyle(plot_color, 0.25f);

						if (reduction_factor > 100) {
							ImPlot::PlotShadedG(shaded_name.c_str(), plotDictStdMinus, &plot_data, plotDictStdPlus,
												&plot_data, padded_count);

						} else {
							ImPlot::PlotShadedG(shaded_name.c_str(), plotDictMin, &plot_data, plotDictMax, &plot_data,
												padded_count);
						}
					} else {
						ImPlot::PlotLineG(col.name.c_str(), plotDict, &plot_data, padded_count);
					}

					break;
			}

			if ((app_state.always_show_cursor || app_state.is_ctrl_pressed) &&
				app_state.global_x_mouse_position >= static_cast<double>(col.timestamp.front()) &&
				app_state.global_x_mouse_position <= static_cast<double>(col.timestamp.back())) {
				ImPlot::SetNextLineStyle(cursor_color, 2.0f);
				ImPlot::PlotInfLines(inf_line_name.c_str(), &app_state.global_x_mouse_position, 1);
			}

			ImPlot::EndPlot();
		}
	}
}  // namespace

auto plotDataInSubplots(std::vector<data_dict_t> &data, const std::string &uuid) -> void {
	const auto plot_size = ImGui::GetContentRegionAvail();

	static auto data_filter = [](const auto &dct) { return dct.visible; };

	const auto n_selected =
		static_cast<int>(std::count_if(data.begin(), data.end(), data_filter));

	if (n_selected == 0) {
		return;
	}

	const auto [rows, cols] = [n_selected]() -> std::pair<int, int> {
		if (n_selected <= 3) {
			return {n_selected, 1};
		}

		return {(n_selected + 1) / 2, 2};
	}();

	auto& app_state = AppState::getInstance();
	const auto global_x_link = app_state.global_x_link;

	if (global_x_link && (std::isnan(app_state.global_link.first) || std::isnan(app_state.global_link.second))) {
		app_state.global_link = getPaddedXLims(data);
	}

	const auto subplot_id = "##" + uuid;
	const auto subplot_flags =
		(n_selected > 1 ? ImPlotSubplotFlags_ShareItems : 0) | (!global_x_link ? ImPlotSubplotFlags_LinkAllX : 0);

	static const auto color_map = [&] -> std::vector<ImVec4> {
		const auto n_colors = ImPlot::GetColormapSize();
		std::vector<ImVec4> colors{};
		colors.reserve(coerceCast<size_t>(n_colors));

		for (int i = 0; i < n_colors; ++i) {
			colors.push_back(ImPlot::GetColormapColor(i));
		}

		return colors;
	}();

	if (ImPlot::BeginSubplots(subplot_id.c_str(), rows, cols, plot_size, subplot_flags)) {
		if (!global_x_link) {
			fixSubplotRanges(data);
		}

		const auto window_date_range = getXLims(data);

		for (int i = 0; auto &col : data | std::views::filter(data_filter)) {
			doPlot(i, n_selected, cols, col, color_map[coerceCast<size_t>(i) % color_map.size()], window_date_range);
			++i;
		}

		ImPlot::EndSubplots();
	}
}
