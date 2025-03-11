#include "plotting.hpp"

#include <algorithm>
#include <array>
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
#include "math_helpers.hpp"
#include "spdlog/spdlog.h"
#include "utility.hpp"
#include "window_context.hpp"

namespace {
	struct plot_data_t {
		const data_dict_t *data;
		size_t reduction_factor;
		size_t start_index;
		int count;

		std::pair<double, double> linked_date_range;
	};

	constexpr auto reduction_steps =
		std::array{1uz,		 10uz,	   50uz,	  100uz,	 500uz,		  1'000uz,	   5'000uz,
				   10'000uz, 50'000uz, 100'000uz, 500'000uz, 1'000'000uz, 10'000'000uz};

	constexpr auto getNextReductionFactor(size_t requested_factor) -> size_t {
		const auto it =
			std::ranges::find_if(reduction_steps, [requested_factor](const auto &e) { return e >= requested_factor; });
		return it != reduction_steps.end() ? *it : reduction_steps.back();
	}

	auto calculateFullZoomReductionFactor(const data_dict_t &dict) -> size_t {
		const auto max_points = AppState::getInstance().max_data_points;
		if (max_points <= 0) {
			return 1;
		}

		return getNextReductionFactor(dict.data->size() / static_cast<size_t>(max_points));
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

		if (i == plot_data.count - 3) {
			return {std::numeric_limits<double>::quiet_NaN(), dd.fit_zoom_range.first};
		}

		if (i == plot_data.count - 2) {
			return {std::numeric_limits<double>::quiet_NaN(), dd.fit_zoom_range.second};
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

	auto createSegments(std::span<const time_t> timestamps, time_t gap_threshold)
		-> std::vector<std::pair<size_t, size_t>> {
		std::vector<std::pair<size_t, size_t>> segments;
		size_t segment_start = 0;

		for (size_t j = 1; j < timestamps.size(); ++j) {
			const auto ts_diff = timestamps[j] - timestamps[j - 1];
			if (ts_diff > gap_threshold) {
				segments.emplace_back(segment_start, j - 1);
				segment_start = j;
			}
		}

		if (segment_start < timestamps.size()) {
			segments.emplace_back(segment_start, timestamps.size() - 1);
		}

		if (segments.empty()) {
			segments.emplace_back(0, timestamps.size() - 1);
		}

		return segments;
	}

	auto calculateAggregates(const data_dict_t &dict, size_t reduction_factor) -> std::vector<data_aggregate_t> {
		const auto segments = createSegments(std::span{*dict.timestamp}, dict.delta_t * 10);

		std::vector<data_aggregate_t> aggregates{};
		aggregates.reserve((dict.data->size() / reduction_factor) + segments.size() + 1);

		for (const auto& segment: segments) {
			const auto segment_value_span =
				std::span{*dict.data}.subspan(segment.first, segment.second - segment.first + 1);
			const auto segment_date_span =
				std::span{*dict.timestamp}.subspan(segment.first, segment.second - segment.first + 1);

			for (size_t i = 0; i < segment_value_span.size(); i += reduction_factor) {
				if (i >= segment_value_span.size()) {
					break;
				}

				const auto count = std::min(reduction_factor, segment_value_span.size() - i);
				const auto value_span = segment_value_span.subspan(i, count);
				const auto date_span = segment_date_span.subspan(i, count);

				if (count >= 3) {
					const auto mean = calcMean<double>(value_span);
					const auto stdev = calcStd<double>(value_span, mean);
					const auto min = calcMin<double>(value_span);
					const auto max = calcMax<double>(value_span);

					aggregates.push_back({.date = date_span.front(),
										  .min = min,
										  .max = max,
										  .mean = mean,
										  .std = stdev,
										  .first = value_span.front()});
				} else {
					aggregates.push_back({.date = date_span.front(),
										  .min = value_span.front(),
										  .max = value_span.front(),
										  .mean = value_span.front(),
										  .std = 0,
										  .first = value_span.front()});
				}
			}

			aggregates.push_back({.date = segment_date_span.back(),
								  .min = std::numeric_limits<double>::quiet_NaN(),
								  .max = std::numeric_limits<double>::quiet_NaN(),
								  .mean = std::numeric_limits<double>::quiet_NaN(),
								  .std = std::numeric_limits<double>::quiet_NaN(),
								  .first = std::numeric_limits<double>::quiet_NaN()});
		}

		aggregates.shrink_to_fit();
		return aggregates;
	}

	auto getValueRangeAggregated(const data_dict_t &dict, size_t reduction_factor) -> std::pair<double, double> {
		auto max_val = std::numeric_limits<double>::lowest();
		auto min_val = std::numeric_limits<double>::max();

		for (size_t i = 0; i < dict.data->size(); i += reduction_factor) {
			if (i >= dict.data->size()) {
				break;
			}

			const auto count = std::min(reduction_factor, dict.data->size() - i);

			if (reduction_factor == 1) {
				max_val = std::max(max_val, dict.data->at(i));
				min_val = std::min(min_val, dict.data->at(i));
			} else if (reduction_factor <= 100) {
				const auto min = calcMin<double>(std::span{*dict.data}.subspan(i, count));
				const auto max = calcMax<double>(std::span{*dict.data}.subspan(i, count));
				
				max_val = std::max(max_val, max);
				min_val = std::min(min_val, min);
			} else {
				const auto mean = calcMean<double>(std::span{*dict.data}.subspan(i, count));
				const auto stdev = calcStd<double>(std::span{*dict.data}.subspan(i, count), mean);
				
				max_val = std::max(max_val, mean + stdev);
				min_val = std::min(min_val, mean - stdev);
			}
		}

		return {min_val, max_val};
	}

	auto recalculateFitZoomRange(data_dict_t &dict) -> void {
		const auto max_data_points = AppState::getInstance().max_data_points;
		if (dict.fit_zoom_calculated_for_points != max_data_points) {
			const auto full_reduction_factor = calculateFullZoomReductionFactor(dict);
			dict.fit_zoom_range = getValueRangeAggregated(dict, full_reduction_factor);
			dict.fit_zoom_calculated_for_points = max_data_points;
		}
	}

	auto checkAggregate(data_dict_t& dict, size_t reduction_factor) -> void {
		if (dict.aggregated_to == reduction_factor && !dict.aggregates.empty()) {
			return;
		}

		spdlog::debug("recalculating aggregates for {} with reduction factor {}", dict.name, reduction_factor);

		dict.aggregates = calculateAggregates(dict, reduction_factor);
		dict.aggregated_to = reduction_factor;

		spdlog::debug("recalculated aggregates for {} with reduction factor {}", dict.name, reduction_factor);
	}

	auto getDateRange(const data_dict_t &data) -> std::pair<double, double> {
		if (data.timestamp->empty()) {
			return {0, 0};
		}

		const auto date_min = static_cast<double>(data.timestamp->front());
		const auto date_max = static_cast<double>(data.timestamp->back());

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

			if (col.timestamp->empty()) {
				continue;
			}

			date_min = std::min(date_min, col.timestamp->front());
			date_max = std::max(date_max, col.timestamp->back());
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

	auto getPaddedYLims(const data_dict_t &col) -> std::pair<double, double> {
		const auto data_min = col.fit_zoom_range.first;
		const auto data_max = col.fit_zoom_range.second;

		const auto padding_percent = ImPlot::GetStyle().FitPadding.y;
		const auto full_range = data_max - data_min;
		const auto padding = full_range * static_cast<double>(padding_percent);

		return {data_min - padding, data_max + padding};
	}

	auto fixSubplotRanges(const std::vector<data_dict_t> &data) -> void {
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

			if (col.timestamp->empty()) {
				continue;
			}

			data_min = std::min(data_min, *std::ranges::min_element(*col.data));
			data_max = std::max(data_max, *std::ranges::max_element(*col.data));
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

	auto getValueOf(const data_dict_t &col, double position) -> std::pair<double, double> {
		const auto it = std::ranges::lower_bound(*col.timestamp, static_cast<time_t>(position));
		if (it == col.timestamp->end()) {
			return {col.timestamp->back(), col.data->back()};
		}

		const auto index = static_cast<size_t>(it - col.timestamp->begin());
		if (index == 0) {
			return {col.timestamp->front(), col.data->front()};
		}

		if (index > 0) {
			const auto prev_index = index - 1;
			const auto prev_time = static_cast<double>(col.timestamp->at(prev_index));
			const auto next_time = static_cast<double>(col.timestamp->at(index));

			if (position - prev_time < next_time - position) {
				return {prev_time, col.data->at(prev_index)};
			}
		}

		return {col.timestamp->at(index), col.data->at(index)};
	}

	auto getAnnotationOffset(const double &val_x, const double &val_y) -> ImVec2 {
		const auto limits = ImPlot::GetPlotLimits(ImAxis_X1, ImPlot::GetCurrentPlot()->CurrentY);

		const auto x_range = limits.X.Max - limits.X.Min;
		const auto x_pos = (val_x - limits.X.Min) / x_range;
		const auto y_range = limits.Y.Max - limits.Y.Min;
		const auto y_pos = (val_y - limits.Y.Min) / y_range;

		const auto is_right_bound = x_pos > 0.5;
		const auto is_top_bound = y_pos > 0.5;

		return {is_right_bound ? -15.0f : 15.0f, is_top_bound ? 15.0f : -15.0f};
	}

	auto drawTag(const data_dict_t &col, const ImVec4 &plot_color) -> void {
		if (col.data_type != data_type_t::FLOAT) {
			return;
		}

		auto &app_state = AppState::getInstance();

		if ((app_state.always_show_cursor || app_state.is_ctrl_pressed) &&
			app_state.global_x_mouse_position >= static_cast<double>(col.timestamp->front()) &&
			app_state.global_x_mouse_position <= static_cast<double>(col.timestamp->back())) {
				const auto scatter_line_name = "##" + col.uuid + "scatter_line_y";
				const auto [val_x, val_y] = getValueOf(col, app_state.global_x_mouse_position);

				const auto value_string = fmt::format("{:g}{}{}", val_y, col.unit.empty() ? "" : " ", col.unit);				
				const auto annotation_offset = getAnnotationOffset(val_x, val_y);

				ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.25f);
				ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 5.0f, plot_color, IMPLOT_AUTO, plot_color);
				ImPlot::PlotScatter(scatter_line_name.c_str(), &val_x, &val_y, 1);
				ImPlot::PopStyleVar();
				ImPlot::Annotation(val_x, val_y, plot_color, annotation_offset, false, "%s", value_string.c_str());
			}
	}

	auto plotSingleMesurement(data_dict_t &col, const ImVec4 &plot_color, const std::pair<double, double> &date_lims)
		-> void {
		auto &app_state = AppState::getInstance();
		const auto max_data_points = static_cast<size_t>(std::max(app_state.max_data_points, 1));
		
		const auto limits = ImPlot::GetPlotLimits(ImAxis_X1);
		const auto [start_index, stop_index] = getIndicesFromTimeRange(*col.timestamp, limits.X);
		const auto points_in_range = stop_index - start_index;
		const auto reduction_factor =
			std::clamp(fastCeil<size_t>(points_in_range, max_data_points), 1uz, std::numeric_limits<size_t>::max());
		const auto reduction_factor_stepped = getNextReductionFactor(reduction_factor);

		checkAggregate(col, reduction_factor_stepped);

		const auto [start_index_agg, stop_index_agg] = getIndicesFromAggregate(col.aggregates, limits.X);
		const auto count = [&]() -> int {
			auto temp = stop_index_agg - start_index_agg + 1;
			temp = std::clamp(temp, 0uz, col.aggregates.size());
			return static_cast<int>(temp);
		}();
		const auto padded_count = count + 4;

		plot_data_t plot_data{.data = &col,
							  .reduction_factor = reduction_factor_stepped,
							  .start_index = start_index_agg,
							  .count = padded_count,
							  .linked_date_range = date_lims};

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

				if (reduction_factor >= 100) {
					ImPlot::SetNextFillStyle(plot_color, 0.25f);
					ImPlot::PlotShadedG(shaded_name.c_str(), plotDictStdMinus, &plot_data, plotDictStdPlus, &plot_data,
										padded_count);

				} else {
					const auto cursor_color = getCursorColor();
					ImPlot::SetNextFillStyle(cursor_color, 0.25f);
					ImPlot::PlotShadedG(shaded_name.c_str(), plotDictMin, &plot_data, plotDictMax, &plot_data,
										padded_count);
				}
			} else {
				ImPlot::PlotLineG(col.name.c_str(), plotDict, &plot_data, padded_count);
			}

			break;
		}

		drawTag(col, plot_color);
	}

	auto getFormatString(const data_dict_t &col) -> std::string {
		if (col.unit.empty()) {
			return "%g";
		}

		if (col.unit == "%") {
			return "%g%%";
		}

		return "%g " + col.unit;
	}

	struct axes_spec_t {
		ImAxis axis{ImAxis_Y1};
		ImVec4 color{IMPLOT_AUTO_COL};
		data_dict_t *col{nullptr};
	};

	auto prepareAxes(std::vector<std::string> &assigned_plot_ids, std::vector<data_dict_t> &data,
					 const std::vector<ImVec4> &color_map, const bool is_x_linked) -> std::vector<axes_spec_t> {
		auto &app_state = AppState::getInstance();

		static constexpr auto axes = std::array{
			ImAxis_Y1,
			ImAxis_Y2,
			ImAxis_Y3
		};

		ImPlot::SetupAxis(ImAxis_X1, "date", ImPlotAxisFlags_NoLabel);
		ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
		ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_NoMenus);

		const auto date_lims = [&]() {
			if (is_x_linked) {
				return app_state.date_range;
			}

			return getPaddedXLims(data);
		}();

		const auto require_reset = [&]() -> bool {
			if (is_x_linked) {
				return false;
			}

			const auto current_x_lims = ImPlot::GetCurrentPlot()->Axes[ImAxis_X1].Range;
			return current_x_lims.Max < date_lims.first || current_x_lims.Min > date_lims.second;
		}();

		ImPlot::SetupAxisLimits(ImAxis_X1, date_lims.first, date_lims.second,
								require_reset ? ImGuiCond_Always : ImGuiCond_Once);

		std::vector<axes_spec_t> axes_specs{};
		axes_specs.reserve(3);

		const auto old_assigned_plot_ids = assigned_plot_ids;
		assigned_plot_ids.clear();

		for (size_t i = 0; auto &col : data) {
			if (!col.visible) {
				continue;
			}

			if (col.timestamp->empty()) {
				continue;
			}

			if (i >= axes.size()) {
				break;
			}

			const auto axis = axes[i];

			const axes_spec_t spec{.axis = axis, .color = color_map[i % color_map.size()], .col = &col};
			const auto is_new_data = i < old_assigned_plot_ids.size() ? old_assigned_plot_ids[i] != col.uuid : true;

			ImPlot::SetupAxis(axis, col.name.c_str(), i % 2 != 0 ? ImPlotAxisFlags_Opposite : ImPlotAxisFlags_None);
			ImPlot::SetupAxisFormat(axis, getFormatString(col).c_str());

			const auto data_lims = getPaddedYLims(col);
			ImPlot::SetupAxisLimits(axis, data_lims.first, data_lims.second,
									is_new_data ? ImGuiCond_Always : ImGuiCond_Once);

			axes_specs.push_back(spec);
			assigned_plot_ids.push_back(col.uuid);
			++i;
		}

		return axes_specs;
	}

	auto drawCursor(const data_dict_t &col) -> void {
		auto &app_state = AppState::getInstance();
		const auto cursor_color = getCursorColor();
		const auto inf_line_name = "##" + col.uuid + "inf_line";

		if (ImPlot::IsPlotHovered()) {
			app_state.global_x_mouse_position =
				ImPlot::GetCurrentPlot()->XAxis(0).PixelsToPlot(ImGui::GetIO().MousePos.x);
		}

		if ((app_state.always_show_cursor || app_state.is_ctrl_pressed) &&
			app_state.global_x_mouse_position >= static_cast<double>(col.timestamp->front()) &&
			app_state.global_x_mouse_position <= static_cast<double>(col.timestamp->back())) {
			ImPlot::SetNextLineStyle(cursor_color, 1.0f);
			ImPlot::PlotInfLines(inf_line_name.c_str(), &app_state.global_x_mouse_position, 1);
		}
	}

	auto doPlotSingle(const axes_spec_t &axis_spec, bool is_x_linked) -> void {
		assert(axis_spec.col != nullptr);
		
		const auto date_lims = [&]() {
			if (is_x_linked) {
				auto &app_state = AppState::getInstance();
				return app_state.date_range;
			}

			return getDateRange(*axis_spec.col);
		}();

		drawCursor(*axis_spec.col);

		ImPlot::SetAxis(axis_spec.axis);
		plotSingleMesurement(*axis_spec.col, axis_spec.color, date_lims);
	}

	// NOLINTNEXTLINE(readability-function-cognitive-complexity)
	auto doPlotSubplots(int current_pos, int n_selected, int col_count, data_dict_t &col, const ImVec4 &plot_color,
						const std::pair<double, double> &window_date_range, bool is_x_global_linked) -> void {
		auto &app_state = AppState::getInstance();
		double &global_link_min = app_state.global_link.first;
		double &global_link_max = app_state.global_link.second;

		const auto *current_subplot = ImPlot::GetCurrentContext()->CurrentSubplot;
		const auto current_flags = current_subplot->Flags;
		const auto is_x_linked = is_x_global_linked || (current_flags & ImPlotSubplotFlags_LinkAllX) != 0 ||
								 (current_flags & ImPlotSubplotFlags_LinkCols) != 0;

		if (is_x_global_linked) {
			ImPlot::SetNextAxisLinks(ImAxis_X1, &global_link_min, &global_link_max);
		}

		const auto plot_title = col.name + "##" + col.uuid;
		if (ImPlot::BeginPlot(plot_title.c_str(), ImVec2(-1, 0),
							  (n_selected < 1 ? ImPlotFlags_NoLegend : 0) | ImPlotFlags_NoTitle)) {
			const auto show_x_axis = [&]() -> bool {
				if (current_pos >= n_selected - col_count) {
					return true;
				}

				return (!is_x_linked && !is_x_global_linked);
			}();

			const auto x_axis_flags = (!show_x_axis ? ImPlotAxisFlags_NoTickLabels : 0) | ImPlotAxisFlags_NoLabel;

			ImPlot::SetupAxes("date", col.name.c_str(), x_axis_flags);
			ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
			ImPlot::SetupAxisFormat(ImAxis_Y1, getFormatString(col).c_str());
			ImPlot::SetupLegend(ImPlotLocation_North, ImPlotLegendFlags_Horizontal | ImPlotLegendFlags_NoMenus);

			const auto date_range = [is_x_linked, is_x_global_linked, current_subplot, &col, &global_link_min,
									 &global_link_max]() {
				if (is_x_linked) {
					if (is_x_global_linked) {
						return std::pair{global_link_min, global_link_max};
					}

					const auto min = current_subplot->ColLinkData[0].Min;
					const auto max = current_subplot->ColLinkData[0].Max;

					return std::pair{min, max};
				}

				return getDateRange(col);
			}();

			const auto require_reset = [is_x_linked, is_x_global_linked, n_selected, &col]() -> bool {
				if (is_x_global_linked || (is_x_linked && n_selected > 1)) {
					return false;
				}

				const auto date_range = getDateRange(col);
				const auto current_x_lims = ImPlot::GetCurrentPlot()->Axes[ImAxis_X1].Range;
				return current_x_lims.Max < date_range.first || current_x_lims.Min > date_range.second;
			}();

			ImPlot::SetupAxisLimits(ImAxis_X1, date_range.first, date_range.second,
									require_reset ? ImGuiCond_Always : ImGuiCond_Once);

			const auto date_lims = [&]() {
				if (is_x_linked) {
					if (is_x_global_linked) {
						return app_state.date_range;
					}
	
					return window_date_range;
				}
	
				return getDateRange(col);
			}();

			plotSingleMesurement(col, plot_color, date_lims);

			drawCursor(col);

			ImPlot::EndPlot();
		}
	}
}  // namespace

auto plotDataInSubplots(WindowContext &window_context) -> void {
	const auto plot_size = ImGui::GetContentRegionAvail();

	static auto data_filter = [](const auto &dct) { return dct.visible; };

	auto &data = window_context.getData();

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

	if (window_context.getForceSubplot() &&
		(std::isnan(app_state.global_link.first) || std::isnan(app_state.global_link.second))) {
		app_state.global_link = getPaddedXLims(data);
	}

	const auto subplot_id = "##" + window_context.getUUID();

	static const auto color_map = [&] -> std::vector<ImVec4> {
		const auto n_colors = ImPlot::GetColormapSize();
		std::vector<ImVec4> colors{};
		colors.reserve(coerceCast<size_t>(n_colors));

		for (int i = 0; i < n_colors; ++i) {
			colors.push_back(ImPlot::GetColormapColor(i));
		}

		return colors;
	}();

	for (auto& col : data | std::views::filter(data_filter)) {
		recalculateFitZoomRange(col);
	}

	const auto is_x_linked = window_context.getGlobalXLink();

	if (window_context.getForceSubplot() || n_selected > 2) {
		const auto subplot_flags =
			(n_selected > 1 ? ImPlotSubplotFlags_ShareItems : 0) | (!is_x_linked ? ImPlotSubplotFlags_LinkAllX : 0);
		
			if (ImPlot::BeginSubplots(subplot_id.c_str(), rows, cols, plot_size, subplot_flags)) {
			if (!is_x_linked) {
				fixSubplotRanges(data);
			}

			const auto window_date_range = getXLims(data);

			for (int i = 0; auto &col : data | std::views::filter(data_filter)) {
				doPlotSubplots(i, n_selected, cols, col, color_map[coerceCast<size_t>(i) % color_map.size()],
							   window_date_range, is_x_linked);
				++i;
			}

			ImPlot::EndSubplots();
		}
	} else {
		if (is_x_linked) {
			ImPlot::SetNextAxisLinks(ImAxis_X1, &app_state.global_link.first, &app_state.global_link.second);
		}

		if (ImPlot::BeginPlot(subplot_id.c_str(), plot_size, ImPlotFlags_NoTitle)) {
			for (const auto &e : prepareAxes(window_context.getAssignedPlotIDsRef(), data, color_map, is_x_linked)) {
				doPlotSingle(e, is_x_linked);
			}

			ImPlot::EndPlot();
		}
	}
}
