#include "frame_inspector.hpp"

#include <cmath>
#include <ctime>
#include <string>
#include <unordered_map>

#include "IconsFontAwesome6.h"
#include "binary_handling.hpp"
#include "fmt/chrono.h"
#include "fmt/format.h"
#include "glaze/json/generic.hpp"
#include "global_state.hpp"
#include "imgui.h"
#include "implot.h"

namespace {
	// The inspector only ever shows one frame at a time, so a single parsed frame is all we keep.
	// Re-reading and re-parsing on a change costs a seek plus ~15 kB of JSON.
	struct metadata_cache_t {
		std::string window_uuid{};
		size_t frame_index{0};
		bool valid{false};
		glz::generic metadata{};
		std::unordered_map<std::string, int> decimals{};
	};

	auto getCache() -> metadata_cache_t& {
		static metadata_cache_t cache{};
		return cache;
	}

	// channel_attributes.logging_config.data_sources tells us how many decimals each measurement
	// value is meant to be shown with.
	auto collectDecimals(const glz::generic& root) -> std::unordered_map<std::string, int> {
		std::unordered_map<std::string, int> decimals{};

		if (!root.is_object() || !root.contains("channel_attributes")) {
			return decimals;
		}

		const auto& attributes = root.at("channel_attributes");

		if (!attributes.is_object() || !attributes.contains("logging_config")) {
			return decimals;
		}

		const auto& logging_config = attributes.at("logging_config");

		if (!logging_config.is_object() || !logging_config.contains("data_sources")) {
			return decimals;
		}

		const auto& data_sources = logging_config.at("data_sources");

		if (!data_sources.is_array()) {
			return decimals;
		}

		for (const auto& entry : data_sources.get_array()) {
			if (!entry.is_object() || !entry.contains("identifier") || !entry.contains("decimals")) {
				continue;
			}

			const auto* identifier = entry.at("identifier").get_if<std::string>();
			const auto* value = entry.at("decimals").get_if<double>();

			if (identifier != nullptr && value != nullptr) {
				decimals[*identifier] = static_cast<int>(*value);
			}
		}

		return decimals;
	}

	auto updateCache(const BinaryWindowContext& window_context, size_t frame_index) -> const metadata_cache_t& {
		auto& cache = getCache();

		if (cache.valid && cache.window_uuid == window_context.getUUID() && cache.frame_index == frame_index) {
			return cache;
		}

		cache.window_uuid = window_context.getUUID();
		cache.frame_index = frame_index;
		cache.valid = false;
		cache.metadata.reset();
		cache.decimals.clear();

		const auto json = readFrameMetadata(window_context.getBinaryData(), frame_index);

		if (json.empty()) {
			return cache;
		}

		if (const auto ec = glz::read_json(cache.metadata, json); ec) {
			return cache;
		}

		cache.decimals = collectDecimals(cache.metadata);
		cache.valid = true;

		return cache;
	}

	auto formatNumber(double value, const std::unordered_map<std::string, int>& decimals,
					  const std::string& key) -> std::string {
		if (const auto it = decimals.find(key); it != decimals.end() && it->second >= 0) {
			return fmt::format("{:.{}f}", value, it->second);
		}

		if (value == std::floor(value) && std::abs(value) < 1e15) {
			return fmt::format("{:.0f}", value);
		}

		return fmt::format("{:g}", value);
	}

	auto formatScalar(const glz::generic& value, const std::unordered_map<std::string, int>& decimals,
					  const std::string& key) -> std::string {
		if (const auto* boolean = value.get_if<bool>(); boolean != nullptr) {
			return *boolean ? "true" : "false";
		}

		if (const auto* number = value.get_if<double>(); number != nullptr) {
			return formatNumber(*number, decimals, key);
		}

		if (const auto* text = value.get_if<std::string>(); text != nullptr) {
			return *text;
		}

		return "null";
	}

	auto renderNode(const std::string& key, const glz::generic& value,
					const std::unordered_map<std::string, int>& decimals) -> void {
		if (value.is_object()) {
			if (ImGui::TreeNode(key.c_str())) {
				for (const auto& [child_key, child_value] : value.get_object()) {
					renderNode(std::string{child_key}, child_value, decimals);
				}

				ImGui::TreePop();
			}

			return;
		}

		if (value.is_array()) {
			const auto label = fmt::format("{} [{}]", key, value.size());

			if (ImGui::TreeNode(label.c_str())) {
				for (size_t i = 0; const auto& child_value : value.get_array()) {
					renderNode(fmt::format("[{}]", i++), child_value, decimals);
				}

				ImGui::TreePop();
			}

			return;
		}

		ImGui::BulletText("%s: %s", key.c_str(), formatScalar(value, decimals, key).c_str());  // NOLINT(hicpp-vararg)
	}

	auto renderSection(const glz::generic& root, const std::string& key, const char* label,
					   const std::unordered_map<std::string, int>& decimals, bool default_open) -> void {
		if (!root.is_object() || !root.contains(key)) {
			return;
		}

		const auto flags = default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0;

		if (ImGui::CollapsingHeader(label, flags)) {
			for (const auto& [child_key, child_value] : root.at(key).get_object()) {
				renderNode(std::string{child_key}, child_value, decimals);
			}
		}
	}

	// The plots run on local time (ImPlotStyle::UseLocalTime), so the frame timestamp does too.
	auto formatLocalTime(double unix_seconds) -> std::string {
		const auto time = static_cast<std::time_t>(unix_seconds);
		std::tm local{};

#ifdef _WIN32
		if (localtime_s(&local, &time) != 0) {
			return {};
		}
#else
		if (localtime_r(&time, &local) == nullptr) {
			return {};
		}
#endif

		return fmt::format("{:%Y-%m-%d %H:%M:%S}", local);
	}

	auto renderDirectView(const binary_frame_t& frame) -> void {
		const auto height = ImGui::GetContentRegionAvail().y * 0.35f;

		if (frame.directview.empty()) {
			ImGui::TextUnformatted("No DirectView data in this frame.");
			return;
		}

		if (ImPlot::BeginPlot("##DirectView", ImVec2(-1, height),
							  ImPlotFlags_NoLegend | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect)) {
			ImPlot::SetupAxes("sample", "V", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
			ImPlot::PlotLine("DirectView", frame.directview.data(), static_cast<int>(frame.directview.size()));
			ImPlot::EndPlot();
		}
	}
}  // namespace

auto renderFrameInspector(const BinaryWindowContext& window_context) -> void {
	const auto& binary_data = window_context.getBinaryData();

	if (binary_data.frames.empty()) {
		ImGui::TextUnformatted("No frames loaded.");
		return;
	}

	const auto frame_index = window_context.getFrameIndexForTime(AppState::getInstance().global_x_mouse_position);
	const auto& frame = binary_data.frames[frame_index];

	ImGui::Text(ICON_FA_CLOCK " %s", formatLocalTime(frame.t0).c_str());  // NOLINT(hicpp-vararg)
	ImGui::Text("frame %zu/%zu · %s · %zu samples @ %.3f ms",  // NOLINT(hicpp-vararg)
				frame_index + 1, binary_data.frames.size(), std::string{frameTypeName(frame.type)}.c_str(),
				frame.sample_count, frame.dt * 1000.0);

	ImGui::Separator();

	renderDirectView(frame);

	ImGui::Separator();

	const auto& cache = updateCache(window_context, frame_index);

	if (!cache.valid) {
		ImGui::TextUnformatted("No metadata in this frame.");
		return;
	}

	ImGui::BeginChild("##frame_metadata");
	renderSection(cache.metadata, "channel_data", "Measurement data", cache.decimals, true);
	renderSection(cache.metadata, "channel_attributes", "Settings", cache.decimals, false);
	ImGui::EndChild();
}
