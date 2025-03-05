#pragma once

#include <string_view>

#include "fmt/format.h"
#include "imgui.h"

namespace ImGuiExt {
	// NOLINTBEGIN(readability-identifier-naming)
	auto GetTextWrapPos() -> float;
	auto TextUnformatted(std::string_view text) -> void;
	auto TextUnformattedCentered(const std::string& text) -> void;
	auto Hyperlink(const char *label, const ImVec2 &size_arg = ImVec2(0, 0), ImGuiButtonFlags flags = 0) -> bool;
	auto HyperlinkCentered(const char *label, const ImVec2 &size_arg = ImVec2(0, 0), ImGuiButtonFlags flags = 0)
		-> bool;

	auto BeginSubWindow(const char *label, bool *collapsed = nullptr, ImVec2 size = ImVec2(0, 0),
						ImGuiChildFlags flags = ImGuiChildFlags_None) -> bool;
	auto EndSubWindow() -> void;

	template <typename... Args>
	auto TextFormatted(fmt::format_string<Args...> fmt, Args&&... args) -> void {
		const auto text = fmt::format(fmt, std::forward<decltype(args)>(args)...);
		ImGui::Text("%s", text.c_str());  // NOLINT(hicpp-vararg)
	}

	template <typename... Args>
	auto TextFormattedCentered(fmt::format_string<Args...> fmt, Args&&... args) -> void {
		const auto text = fmt::format(fmt, std::forward<decltype(args)>(args)...);
		return TextUnformattedCentered(text);
	}

	template <typename... Args>
	auto TextFormattedWrapped(fmt::format_string<Args...> fmt, Args&&... args) -> void {
		const bool need_backup =
			ImGuiExt::GetTextWrapPos() < 0.0f;	// Keep existing wrap position if one is already set

		if (need_backup) {
			ImGui::PushTextWrapPos(0.0f);
		}

		ImGuiExt::TextFormatted(fmt, std::forward<decltype(args)>(args)...);

		if (need_backup) {
			ImGui::PopTextWrapPos();
		}
	}

	template <typename... Args>
	auto TextFormattedDisabled(fmt::format_string<Args...> fmt, Args&&... args) -> void {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGuiExt::TextFormatted(fmt, std::forward<decltype(args)>(args)...);
        ImGui::PopStyleColor();
    }
	// NOLINTEND(readability-identifier-naming)
}  // namespace ImGuiExt