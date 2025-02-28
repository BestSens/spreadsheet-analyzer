#include "about_screen.hpp"

#include <array>

#include "SDL3/SDL.h"
#include "fmt/format.h"
#include "fonts.hpp"
#include "imgui.h"
#include "imgui_extensions.hpp"
#include "imgui_internal.h"
#include "version.hpp"
#include "winapi.hpp"

extern SDL_Surface* window_icon;  //NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

namespace {
	struct Library {
		const char *name;
		const char *author;
		const char *link;
	};

	constexpr std::array libraries = {
		Library{"ImGui", "ocornut", "https://github.com/ocornut/imgui"},
		Library{"ImPlot", "epezent", "https://github.com/epezent/implot"},
		Library{"fmt", "fmtlib", "https://github.com/fmtlib/fmt"},
		Library{"nativefiledialog-extended", "btzy", "https://github.com/btzy/nativefiledialog-extended"},
		Library{"spdlog", "gabime", "https://github.com/gabime/spdlog"},
		Library{"csv-parser", "vincentlaucsb", "https://github.com/vincentlaucsb/csv-parser/"},
		Library{"stduuid", "mariusbancila", "https://github.com/mariusbancila/stduuid/"},
		Library{"cxxopts", "jarro2783", "https://github.com/jarro2783/cxxopts"},
		Library{"SDL", "libsdl-org", "https://github.com/libsdl-org/SDL"},
		Library{"SDL_image", "libsdl-org", "https://github.com/libsdl-org/SDL_image"},
		Library{"expected", "TartanLlama", "https://github.com/TartanLlama/expected"},
		Library{"roboto", "google", "https://fonts.google.com/specimen/Roboto"},
	};
}

auto showAboutScreen(bool& show_about, SDL_Renderer* renderer) -> void {
	if (!show_about) {
		return;
	}

	const auto viewport_size = ImGui::GetIO().DisplaySize;
	const auto window_size = ImVec2{500, 700};
	const auto window_pos = ImVec2{(viewport_size.x - window_size.x) * 0.5f, (viewport_size.y - window_size.y) * 0.5f};
	
	ImGui::SetNextWindowSize(window_size, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(window_pos, ImGuiCond_FirstUseEver);
	ImGui::Begin("About", &show_about,
				 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_Modal);

	ImGui::Spacing();
	
	ImGui::PushFont(getFont(fontList::ROBOTO_MONO_20));
	ImGuiExt::TextUnformattedCentered("Spreadsheet Analyzer");
	ImGui::PopFont();
	ImGuiExt::TextUnformattedCentered("Copyright © 2024 BestSens AG");

	if (window_icon != nullptr) {
		static ImTextureID icon_texture;
		static bool initialized = false;

		if (!initialized) {
			const auto* texture = SDL_CreateTextureFromSurface(renderer, window_icon);
			if (texture != nullptr) {
				// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
				icon_texture = reinterpret_cast<ImTextureID>(texture);
			}

			initialized = true;
		}

		// Display the icon with padding
		if (icon_texture != 0) {
			const float icon_size = 128.0f;
			const float padding = 10.0f;  // Padding amount
			
			// Add padding before image
			ImGui::Dummy(ImVec2(0.0f, padding));
			
			// Center and display the image
			const auto window_width = ImGui::GetContentRegionAvail().x;
			ImGui::SetCursorPosX((window_width - icon_size) * 0.5f);
			ImGui::Image(icon_texture, ImVec2(icon_size, icon_size));
			
			// Add padding after image
			ImGui::Dummy(ImVec2(0.0f, padding));
		}
	}

	ImGuiExt::TextFormattedCentered("Version: {}", appVersion());
	ImGuiExt::TextFormattedCentered("Git revision: {}-{}", appGitBranch(), appGitRevision());
	ImGuiExt::TextFormattedCentered("Build date: {}", appCompileDate());
	ImGuiExt::TextFormattedCentered("Built with: {}", appCompilerVersion());

	ImGui::Spacing();

	const auto width = ImGui::GetContentRegionAvail().x;
	if (ImGuiExt::BeginSubWindow("Libraries")) {
		ImGuiExt::TextFormattedWrapped(
			"Spreadsheet analyzer builds on top of the amazing work of a ton of talented library developers "
			"without which this project wouldn't stand.");

		for (const auto &library : libraries) {
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_TableHeaderBg));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 50);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12, 3});

			if (ImGui::BeginChild(
					library.link, ImVec2(),
					ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY)) {
				if (ImGuiExt::Hyperlink(fmt::format("{}/{}", library.author, library.name).c_str())) {
					openWebpage(library.link);
				}
				ImGui::SetItemTooltip("%s", library.link);	// NOLINT(hicpp-vararg)
			}
			ImGui::EndChild();

			ImGui::SameLine();
			if (ImGui::GetCursorPosX() > (width - 150.0f)) {
				ImGui::NewLine();
			}

			ImGui::PopStyleColor();
			ImGui::PopStyleVar(2);
		}
	}
	ImGuiExt::EndSubWindow();

	if (ImGuiExt::BeginSubWindow("License")) {
		static const auto* license_text = R"(MIT License

Copyright (c) 2025 BestSens AG

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.)";
		
		ImGuiExt::TextFormattedWrapped("{}", license_text);
	}
	ImGuiExt::EndSubWindow();
	
	ImGui::End();
}