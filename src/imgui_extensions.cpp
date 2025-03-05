#include "imgui_extensions.hpp"

#include "imgui.h"
#include "imgui_internal.h"

namespace ImGuiExt {
	// NOLINTBEGIN(readability-identifier-naming)
	auto GetTextWrapPos() -> float {
		const auto* window = ImGui::GetCurrentWindowRead();
		return window->DC.TextWrapPos;
	}

	auto TextUnformatted(std::string_view text) -> void {
		ImGui::TextUnformatted(text.data());
	}

	auto TextUnformattedCentered(const std::string& text) -> void {
		const auto window_width = ImGui::GetContentRegionAvail().x;
		const auto text_width = ImGui::CalcTextSize(text.c_str()).x;

		ImGui::SetCursorPosX((window_width - text_width) * 0.5f);
		ImGui::Text("%s", text.c_str());  // NOLINT(hicpp-vararg)
	}

	auto Hyperlink(const char *label, const ImVec2 &size_arg, ImGuiButtonFlags flags) -> bool {
		auto *window = ImGui::GetCurrentWindow();

		const ImGuiID id = window->GetID(label);
		const ImVec2 label_size = ImGui::CalcTextSize(label, nullptr, true);

		auto pos = window->DC.CursorPos;
		auto size = ImGui::CalcItemSize(size_arg, label_size.x, label_size.y);

		const ImRect bb(pos, {pos.x + size.x, pos.y + size.y});
		ImGui::ItemAdd(bb, id);

		bool hovered{false};
		bool held{false};
		const auto pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, flags);

		// Render
		const ImU32 col =
			hovered ? ImGui::GetColorU32(ImGuiCol_ButtonHovered) : ImGui::GetColorU32(ImGuiCol_ButtonActive);
		ImGui::PushStyleColor(ImGuiCol_Text, ImU32(col));
		ImGui::TextEx(label, nullptr, ImGuiTextFlags_NoWidthForLargeClippedText);  // Skip formatting

		if (hovered) {
			ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, pos.y + size.y), {pos.x + size.x, pos.y + size.y},
												ImU32(col));
		}

		ImGui::PopStyleColor();
		return pressed;
	}

	auto HyperlinkCentered(const char *label, const ImVec2 &size_arg, ImGuiButtonFlags flags) -> bool {
		const auto window_width = ImGui::GetContentRegionAvail().x;
		const auto text_width = ImGui::CalcTextSize(label, nullptr, true).x;

		ImGui::SetCursorPosX((window_width - text_width) * 0.5f);
		return Hyperlink(label, size_arg, flags);
	}

	auto BeginSubWindow(const char *label, bool *collapsed, ImVec2 size, ImGuiChildFlags flags) -> bool {
		const bool hasMenuBar = !std::string_view(label).empty();

		bool result = false;
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0F);
		if (ImGui::BeginChild(fmt::format("{}##SubWindow", label).c_str(), size,
							  ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | flags,
							  hasMenuBar ? ImGuiWindowFlags_MenuBar : ImGuiWindowFlags_None)) {
			result = true;

			if (hasMenuBar && ImGui::BeginMenuBar()) {
				if (collapsed == nullptr)
					ImGui::TextUnformatted(label);
				else {
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, ImGui::GetStyle().FramePadding.y));
					ImGui::PushStyleColor(ImGuiCol_Button, 0x00);
					if (ImGui::Button(label))
						*collapsed = !*collapsed;
					ImGui::PopStyleColor();
					ImGui::PopStyleVar();
				}
				ImGui::EndMenuBar();
			}

			if (collapsed != nullptr && *collapsed) {
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (ImGui::GetStyle().FramePadding.y * 2));
				ImGuiExt::TextFormattedDisabled("...");
				result = false;
			}
		}
		ImGui::PopStyleVar();

		return result;
	}

	auto EndSubWindow() -> void {
		ImGui::EndChild();
	}
	// NOLINTEND(readability-identifier-naming)
}  // namespace ImGuiExt