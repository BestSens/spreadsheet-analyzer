#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <list>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Libraries
#include "SDL3/SDL_main.h"
#include "SDL3/SDL_opengl.h"
#include "SDL3_image/SDL_image.h"
#include "about_screen.hpp"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include "cxxopts.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "implot.h"
#include "spdlog/spdlog.h"

// Own headers
#include "csv_handling.hpp"
#include "custom_type_traits.hpp"
#include "dicts.hpp"
#include "file_dialog.hpp"
#include "fonts.hpp"
#include "global_state.hpp"
#include "imgui_extensions.hpp"
#include "plotting.hpp"
#include "winapi.hpp"
#include "window_context.hpp"
#include "IconsFontAwesome6.h"

extern "C" const unsigned char icon_data[];
extern "C" const size_t icon_data_size;

extern "C" const unsigned char logo_data[];
extern "C" const size_t logo_data_size;

namespace {
	auto terminateHandler() -> void {
		const auto ep = std::current_exception();

		if (ep) {
			try {
				spdlog::critical("Terminating with uncaught exception");
				std::rethrow_exception(ep);
			} catch (const std::exception &e) {
				SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Terminating with uncaught exception", e.what(), nullptr);
				spdlog::critical("\twith `what()` = \"{}\"", e.what());
			} catch (...) {
				SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Terminating with uncaught exception", "Unknown error", nullptr);
			}  // NOLINT(bugprone-empty-catch)
		} else {
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Terminating without exception", "Unknown error", nullptr);
			spdlog::critical("Terminating without exception");
		}

		std::exit(EXIT_FAILURE);  // NOLINT(concurrency-mt-unsafe)
	}

	auto updateDateRange(const auto &window_contexts) -> void {
		double date_min = std::numeric_limits<double>::max();
		double date_max = std::numeric_limits<double>::lowest();
		
		for (const auto& window_context : window_contexts) {
			if (!std::holds_alternative<CSVWindowContext>(window_context)) {
				continue;
			}

			const auto &data = std::get<CSVWindowContext>(window_context).getData();

			if (data.empty()) {
				continue;
			}

			for (const auto &e : data) {
				if (!e.visible) {
					continue;
				}

				if (e.timestamp->empty()) {
					continue;
				}

				date_min = std::min(date_min, static_cast<double>(e.timestamp->front()));
				date_max = std::max(date_max, static_cast<double>(e.timestamp->back()));
			}
		}

		if (date_min != std::numeric_limits<double>::max() && date_max != std::numeric_limits<double>::lowest()) {
			AppState::getInstance().date_range = {date_min, date_max};
		}
	}

	auto setSystemLocale() -> void {
		try {
			const auto locale = std::locale("");
			std::locale::global(locale);
		} catch (const std::exception &e) {
			spdlog::warn("Error setting system locale: {}", e.what());
		}
	}

	auto copyString(std::span<char> destination, const std::string_view source) -> void {
		if (destination.empty()) {
			return;
		}

#if defined(_MSC_VER) || defined(__STDC_LIB_EXT1__)
		strncpy_s(destination.data(), destination.size(), source.data(), destination.size() - 1);
#else
		assert(source.size() < destination.size());
		std::strncpy(destination.data(), source.data(),
					 destination.size() - 1);		// NOLINT(bugprone-suspicious-stringview-data-usage)
		destination[destination.size() - 1] = '\0'; // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
#endif
	}
}  // namespace

#ifdef _WIN32
#ifndef DEBUG
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif
#endif
auto main(int argc, char **argv) -> int {  // NOLINT(readability-function-cognitive-complexity)
	std::set_terminate(terminateHandler);

	std::vector<std::filesystem::path> commandline_paths{};

	auto &app_state = AppState::getInstance();

	{
		cxxopts::Options options(argv[0], "Spreadsheet Analyzer");

		options.add_options()
			("h,help", "Print usage")
			("filename", "CSV file to load", cxxopts::value<std::vector<std::string>>(), "FILE")
			("v,verbose", "verbose output")
			;

		try {
			options.parse_positional({"filename"});
			auto result = options.parse(argc, argv);

			if (result.contains("help")) {
				spdlog::info(options.help());
				return EXIT_SUCCESS;
			}

			if (result.contains("filename")) {
				for (const auto& file : result["filename"].as<std::vector<std::string>>()) {
					commandline_paths.emplace_back(file);
				}
			}

			if (result.contains("verbose")) {
				spdlog::set_level(spdlog::level::debug);
				app_state.show_debug_menu = true;
				spdlog::info("verbose output enabled");
			}
		} catch (const std::exception& e) {
			spdlog::critical(e.what());
			return EXIT_FAILURE;
		}

	}

	setSystemLocale();

	auto &window_contexts = AppState::getInstance().window_contexts;

	{
		const auto paths_expanded = preparePaths(commandline_paths);

		if (!paths_expanded.empty()) {
			window_contexts.emplace_back(std::in_place_type<CSVWindowContext>, paths_expanded,
										 CSVWindowContext::function_signature{loadCSVs});
		}
	}

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		spdlog::error("Error: {}", SDL_GetError());
		return -1;
	}

	spdlog::debug("SDL Initialized");
	const SDL_WindowFlags window_flags =
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_MAXIMIZED;
	auto *window = SDL_CreateWindow("Spreadsheet Analyzer", 1280, 720, window_flags);
	app_state.renderer = SDL_CreateRenderer(window, nullptr);
	SDL_SetRenderVSync(app_state.renderer, 1);

	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
	app_state.window_icon = IMG_LoadPNG_IO(SDL_IOFromMem(const_cast<unsigned char*>(icon_data), icon_data_size));
	SDL_SetWindowIcon(window, app_state.window_icon);

	SDL_ShowWindow(window);

	SDL_Texture *logo_texture{};
	ImVec2 logo_size{};
	const float logo_scale = 0.4f;
	{
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
		auto *logo = IMG_LoadPNG_IO(SDL_IOFromMem(const_cast<unsigned char *>(logo_data), logo_data_size));

		if (logo != nullptr) {
			logo_texture = SDL_CreateTextureFromSurface(app_state.renderer, logo);
			
			if (logo_texture == nullptr) {
				spdlog::error("Error: {}", SDL_GetError());
			}

			logo_size = {static_cast<float>(logo->w) * logo_scale, static_cast<float>(logo->h) * logo_scale};

			SDL_DestroySurface(logo);
		}
	}

	app_state.display_scale = SDL_GetWindowDisplayScale(window);
	
	if (!SDL_SetRenderScale(app_state.renderer, app_state.display_scale, app_state.display_scale)) {
		spdlog::warn("Failed to set renderer scale: {}", SDL_GetError());
	}

	spdlog::debug("Display scale: {}x", app_state.display_scale);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
	io.IniFilename = nullptr;

	addFonts();

	io.FontDefault = getFont(fontList::ROBOTO_SANS_16);

	// Setup Dear ImGui style
	try {
		if (isLightTheme()) {
			ImGui::StyleColorsLight();
		} else {
			ImGui::StyleColorsDark();
		}
	} catch (const std::exception &e) {
		spdlog::error("{}", e.what());
		ImGui::StyleColorsDark();
	}

	// Setup Platform/Renderer backends
	ImGui_ImplSDL3_InitForSDLRenderer(window, app_state.renderer);
    ImGui_ImplSDLRenderer3_Init(app_state.renderer);

	const auto background_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	// Main loop
	bool done{false};

	while (!done) {
		bool open_selected{false};
		bool select_folder{false};
		bool force_custom_import{false};
		
		{
			SDL_Event event;
			SDL_WaitEventTimeout(&event, 100);
			ImGui_ImplSDL3_ProcessEvent(&event);
			if (event.type == SDL_EVENT_QUIT) {
				done = true;
			}

			if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) {
				done = true;
			}

			if (event.type == SDL_EVENT_KEY_DOWN) {
				if (event.key.key == SDLK_O && (event.key.mod & SDL_KMOD_CTRL) != 0) {
					open_selected = true;
					select_folder = (event.key.mod & SDL_KMOD_SHIFT) != 0;
				}

				if (event.key.key == SDLK_Q && (event.key.mod & SDL_KMOD_CTRL) != 0) {
					done = true;
				}

				if ((event.key.mod & SDL_KMOD_CTRL) != 0) {
					app_state.is_ctrl_pressed = true;
				}

				if ((event.key.mod & SDL_KMOD_SHIFT) != 0) {
					app_state.is_shift_pressed = true;
				}
			}

			if (event.type == SDL_EVENT_KEY_UP) {
				if ((event.key.mod & SDL_KMOD_CTRL) == 0) {
					app_state.is_ctrl_pressed = false;
				}

				if ((event.key.mod & SDL_KMOD_SHIFT) == 0) {
					app_state.is_shift_pressed = false;
				}
			}

			if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
				app_state.display_scale = SDL_GetWindowDisplayScale(window);
				
				if (!SDL_SetRenderScale(app_state.renderer, app_state.display_scale, app_state.display_scale)) {
					spdlog::warn("Failed to set renderer scale: {}", SDL_GetError());
				}
				
				spdlog::debug("Display scale changed to {}x", app_state.display_scale);
			}
		}

		ImGui_ImplSDLRenderer3_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();
		ImVec2 menu_size{};

		if (ImGui::BeginMainMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				if (ImGui::MenuItem("Open", "Ctrl+O", &open_selected)) {}
				if (ImGui::MenuItem("Open (Manual)")) {
					open_selected = true;
					force_custom_import = true;
				}
				if (ImGui::MenuItem("Open Folder", "Ctrl+Shift+O", &open_selected)) {
					select_folder = true;
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Exit", "Ctrl+Q", &done)) {}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Settings")) {
				ImGui::MenuItem("Always show date cursor", nullptr, &app_state.always_show_cursor);
				ImGui::Separator();
				ImGui::InputInt("Max displayed data points", &app_state.max_data_points, 100, 1'000);
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Help")) {
				ImGui::MenuItem("About", nullptr, &app_state.show_about);
				ImGui::EndMenu();
			}

			if (app_state.show_debug_menu) {
				if (ImGui::BeginMenu("Debug")) {
					ImGui::EndMenu();
				}
			}

			menu_size = ImGui::GetWindowSize();
			ImGui::EndMainMenuBar();
		}

		if (open_selected) {
			const auto paths = selectFilesFromDialog(select_folder);

			if (!paths.empty()) {
				const auto paths_expanded = preparePaths(paths);
				window_contexts.emplace_back(std::in_place_type<CSVWindowContext>, paths_expanded,
				                             CSVWindowContext::function_signature{loadCSVs}, force_custom_import);
			}
		}

		showAboutScreen();

		// CSV import config dialog
		{
			static csv_parse_config_t popup_config{};
			static int delim_choice{1};       // 0=comma 1=semicolon 2=tab 3=other
			static std::string custom_delim{","};
			static int decimal_choice{1};     // 0=period 1=comma
			static std::array<char, 64> date_fmt_buf{};
			static int date_col_choice{0};
			static bool first_row_is_header_choice{true};
			static std::vector<std::string> preview_lines{};

			CSVWindowContext *config_ctx{nullptr};
			for (auto &temp : window_contexts) {
				if (!std::holds_alternative<CSVWindowContext>(temp)) {
					continue;
				}
				auto &ctx = std::get<CSVWindowContext>(temp);
				if (ctx.needsConfigDialog()) {
					config_ctx = &ctx;
					break;
				}
			}

			if (config_ctx != nullptr && !config_ctx->isConfigPopupOpened()) {
				const auto inferred_config = inferCSVParseConfig(config_ctx->getStoredPaths(), config_ctx->getCurrentConfig());
				if (inferred_config.has_value()) {
					const auto &current = config_ctx->getCurrentConfig();
					const bool changed = inferred_config->field_delimiter != current.field_delimiter ||
										 inferred_config->decimal_separator != current.decimal_separator ||
										 inferred_config->date_column_index != current.date_column_index ||
										 inferred_config->date_format != current.date_format;

					if (changed) {
						if (config_ctx->shouldSuggestConfigInDialog()) {
							config_ctx->applySuggestedConfig(*inferred_config);
						} else {
							config_ctx->retryWithConfig(*inferred_config);
						}
					}
				}

				if (config_ctx->getLoadingStatus().is_loading || !config_ctx->needsConfigDialog()) {
					config_ctx = nullptr;
				}
			}

			if (config_ctx != nullptr && !config_ctx->isConfigPopupOpened()) {
				popup_config = config_ctx->getCurrentConfig();
				decimal_choice = (popup_config.decimal_separator == '.') ? 0 : 1;
				
				if (popup_config.field_delimiter == ',') {
					delim_choice = 0;
				} else if (popup_config.field_delimiter == ';') {
					delim_choice = 1;
				} else if (popup_config.field_delimiter == '\t') {
					delim_choice = 2;
				} else {
					delim_choice = 3;
					custom_delim.at(0) = popup_config.field_delimiter;
				}

				copyString(std::span<char>{date_fmt_buf.data(), date_fmt_buf.size()}, popup_config.date_format);
				date_col_choice = static_cast<int>(popup_config.date_column_index);
				first_row_is_header_choice = popup_config.first_row_is_header;

				preview_lines.clear();
				if (!config_ctx->getStoredPaths().empty()) {
					std::ifstream f(config_ctx->getStoredPaths().front());
					std::string line;
					while (std::getline(f, line) && preview_lines.size() < 5) {
						preview_lines.push_back(std::move(line));
					}
				}

				ImGui::OpenPopup("CSV Import Configuration");
				config_ctx->markPopupOpened();
			}

			ImGui::SetNextWindowSize(ImVec2(580, 0), ImGuiCond_Always);
			if (ImGui::BeginPopupModal("CSV Import Configuration", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
				if (config_ctx != nullptr) {
					ImGui::TextDisabled("Parse error: %s", std::string{config_ctx->getParseErrorSample()}.c_str()); // NOLINT(hicpp-vararg)
				}
				ImGui::Spacing();

				if (!preview_lines.empty()) {
					ImGui::SeparatorText("File preview");
					ImGui::PushFont(getFont(fontList::ROBOTO_MONO_16));
					ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
					ImGui::BeginChild("preview", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 5.5f), ImGuiChildFlags_Borders);
					for (const auto &l : preview_lines) {
						ImGui::TextUnformatted(l.c_str());
					}
					ImGui::EndChild();
					ImGui::PopStyleColor();
					ImGui::PopFont();
					ImGui::Spacing();
				}

				ImGui::SeparatorText("Import settings");

				ImGui::Text("Field delimiter");	 // NOLINT(hicpp-vararg)
				ImGui::SameLine(160);
				ImGui::RadioButton("Comma  ,",    &delim_choice, 0); ImGui::SameLine();
				ImGui::RadioButton("Semicolon ;", &delim_choice, 1); ImGui::SameLine();
				ImGui::RadioButton("Tab",         &delim_choice, 2); ImGui::SameLine();
				ImGui::RadioButton("Other",       &delim_choice, 3);
				if (delim_choice == 3) {
					ImGui::SameLine();
					ImGui::SetNextItemWidth(40);
					ImGui::InputText("##custom_delim", custom_delim.data(), custom_delim.size());
				}

				ImGui::Text("Decimal separator");  // NOLINT(hicpp-vararg)
				ImGui::SameLine(160);
				ImGui::RadioButton("Period  .##dec", &decimal_choice, 0); ImGui::SameLine();
				ImGui::RadioButton("Comma  ,##dec", &decimal_choice, 1);

				ImGui::Text("First row");  // NOLINT(hicpp-vararg)
				ImGui::SameLine(160);
				ImGui::Checkbox("Treat first row as header", &first_row_is_header_choice);

				ImGui::Text("Date format");  // NOLINT(hicpp-vararg)
				ImGui::SameLine(160);
				ImGui::SetNextItemWidth(220);
				ImGui::InputTextWithHint("##datefmt", "auto-detect", date_fmt_buf.data(), date_fmt_buf.size());
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
					// NOLINTNEXTLINE(hicpp-vararg)
					ImGui::SetTooltip(
						"strptime format string, e.g.:\n"
						"  %%Y-%%m-%%d %%H:%%M:%%S\n"
						"  %%d.%%m.%%Y %%H:%%M:%%S\n"
						"  %%Y-%%m-%%dT%%H:%%M:%%S\n"
						"Leave empty for automatic detection.");
				}

				ImGui::Text("Date column");  // NOLINT(hicpp-vararg)
				ImGui::SameLine(160);
				{
					const char cur_delim = [&]() -> char {
						switch (delim_choice) {
							case 1:  return ';';
							case 2:  return '\t';
							case 3:  return (!custom_delim.empty()) ? custom_delim.at(0) : ','; 
							default: return ',';
						}
					}();

					std::vector<std::string> header_cols;
					if (!preview_lines.empty()) {
						std::istringstream ss(preview_lines.at(0));
						std::string token;
						int idx = 0;
						while (std::getline(ss, token, cur_delim)) {
							if (first_row_is_header_choice) {
								header_cols.push_back(token.empty() ? fmt::format("col {}", idx) : token);
							} else {
								header_cols.push_back(fmt::format("col {}", idx));
							}
							++idx;
						}
					}

					if (!header_cols.empty()) {
						date_col_choice = std::min(date_col_choice, static_cast<int>(header_cols.size()) - 1);
						const auto preview_label = fmt::format("[{}] {}", date_col_choice, header_cols.at(static_cast<size_t>(date_col_choice)));
						ImGui::SetNextItemWidth(220);
						if (ImGui::BeginCombo("##datecol", preview_label.c_str())) {
						for (int i = 0; i < static_cast<int>(header_cols.size()); ++i) {
							const auto label = fmt::format("[{}]  {}", i, header_cols.at(static_cast<size_t>(i)));
							const auto selected = (date_col_choice == i);
							if (ImGui::Selectable(label.c_str(), selected)) {
								date_col_choice = i;
								}
								if (selected) {
									ImGui::SetItemDefaultFocus();
								}
							}
							ImGui::EndCombo();
						}
					} else {
						ImGui::SetNextItemWidth(80);
						ImGui::InputInt("##datecol", &date_col_choice, 1, 1);
						date_col_choice = std::max(0, date_col_choice);
					}
				}

				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();

				if (ImGui::Button("Apply", ImVec2(120, 0))) {
					switch (delim_choice) {
						case 0: popup_config.field_delimiter = ',';  break;
						case 1: popup_config.field_delimiter = ';';  break;
						case 2: popup_config.field_delimiter = '\t'; break;
						default: popup_config.field_delimiter = (!custom_delim.empty()) ? custom_delim.at(0) : ','; break;
					}
					popup_config.decimal_separator = (decimal_choice == 0) ? '.' : ',';
					popup_config.date_format = std::string{date_fmt_buf.data(), date_fmt_buf.size()};
					popup_config.date_column_index = static_cast<size_t>(std::max(0, date_col_choice));
					popup_config.first_row_is_header = first_row_is_header_choice;
					if (config_ctx != nullptr) {
						config_ctx->retryWithConfig(popup_config);
					}
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel", ImVec2(120, 0))) {
					if (config_ctx != nullptr) {
						config_ctx->cancelConfigDialog();
					}
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}
		}

		const auto dockspace = ImGui::DockSpaceOverViewport(ImGui::GetID("DockSpace"), ImGui::GetMainViewport(),
															ImGuiDockNodeFlags_PassthruCentralNode);
		
		if (app_state.show_debug_menu) {
			ImGui::ShowMetricsWindow();
		}

		updateDateRange(window_contexts);

		for (auto &temp : window_contexts) {
			if (!std::holds_alternative<CSVWindowContext>(temp)) {
				continue;
			}

			auto &ctx = std::get<CSVWindowContext>(temp);
			ctx.checkForFinishedLoading();
			auto &dict = ctx.getData();
			auto &window_open = ctx.getWindowOpenRef();
			
			ImGui::SetNextWindowDockID(dockspace, ImGuiCond_Once);
			ImGui::Begin(ctx.getWindowID().c_str(), &window_open,
						 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_MenuBar);

			const auto loading_status = ctx.getLoadingStatus();

			if (ImGui::BeginMenuBar()) {
				bool &global_x_link = ctx.getGlobalXLinkRef();
				
				ImGui::MenuItem(global_x_link ? ICON_FA_LINK_SLASH : ICON_FA_LINK, nullptr, &global_x_link);
				
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
					if (global_x_link) {
						ImGui::SetTooltip("Unlink x-axes");	 // NOLINT(hicpp-vararg)
					} else {
						ImGui::SetTooltip("Link x-axes");  // NOLINT(hicpp-vararg)
					}
				}

				bool& force_subplot = ctx.getForceSubplotRef();

				ImGui::MenuItem(ICON_FA_TABLE_LIST, nullptr, &force_subplot);

				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
					ImGui::SetTooltip("Force subplots");  // NOLINT(hicpp-vararg)
				}

				if (ImGui::MenuItem(ICON_FA_CLONE, nullptr, nullptr, !loading_status.is_loading)) {
					window_contexts.emplace_back(ctx);
				}

				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
					ImGui::SetTooltip("Duplicate");	 // NOLINT(hicpp-vararg)
				}
				ImGui::EndMenuBar();
			}

			const auto window_content_size = ImGui::GetContentRegionAvail();

			if (loading_status.is_loading) {
				const auto denominator = std::max<size_t>(loading_status.required_files, 1);
				const auto progress_raw = static_cast<float>(loading_status.finished_files) /
									  static_cast<float>(denominator);
				const auto progress = std::clamp(progress_raw, 0.0f, 1.0f);
				const auto label = fmt::format("{:.0f}% ({}/{})", progress * 100.0f, loading_status.finished_files,
											   loading_status.required_files);

				const auto padding = std::min(100.0f, window_content_size.x / 10.0f);
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padding);
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (window_content_size.y / 2.0f) - 10.0f);
				ImGui::ProgressBar(progress, ImVec2(window_content_size.x - (2.0f * padding), 20.0f), label.c_str());
			} else {
				if (!dict.empty()) {
					ImGui::BeginChild("Column List", ImVec2(250, window_content_size.y));
					const auto subwindow_size = ImGui::GetContentRegionAvail();
					if (ImGui::BeginListBox("##List Box", ImVec2(subwindow_size.x, subwindow_size.y))) {
						for (auto &dct : dict) {
							const auto list_id = dct.name + "##" + dct.uuid;
							if (ImGui::Selectable(list_id.c_str(), &dct.visible)) {
								if (app_state.is_ctrl_pressed) {
									break;
								}

								if (app_state.is_shift_pressed) {
									const auto first_visible =
										std::ranges::find_if(dict, [](const auto& tmp) -> bool { return tmp.visible; });

									const auto current_dict = std::ranges::find_if(
										dict, [&dct](const auto& tmp) -> bool { return tmp.uuid == dct.uuid; });

									if (first_visible != dict.end() && current_dict != dict.end()) {
										const auto first_index = std::distance(dict.begin(), first_visible);
										const auto current_index = std::distance(dict.begin(), current_dict);

										const auto start = std::min(first_index, current_index);
										const auto stop = std::max(first_index, current_index);

										std::ranges::for_each(dict.begin() + start, dict.begin() + stop + 1,
															  [](auto& tmp) -> void { tmp.visible = true; });
									}

									break;
								}

								std::ranges::for_each(dict, [](auto &tmp) -> void { tmp.visible = false; });
								dct.visible = true;
							}
						}
						ImGui::EndListBox();
					}
					ImGui::EndChild();

					ImGui::SameLine();

					ImGui::BeginChild("File content", ImVec2(window_content_size.x - 255, window_content_size.y));
					ImGui::PushFont(getFont(fontList::ROBOTO_MONO_16));
					
					ctx.switchToImPlotContext();
					plotDataInSubplots(ctx);
					
					if (app_state.show_debug_menu) {
						ImGui::PushID(ctx.getUUID().c_str());
						ImPlot::ShowMetricsWindow();
						ImGui::PopID();
					}

					ImGui::PopFont();
					ImGui::EndChild();
				} else {
					ImGui::Text("No valid data found.");  // NOLINT(hicpp-vararg)
				}
			}

			ImGui::End();

			if (!window_open) {
				ImGui::ClearWindowSettings(ctx.getWindowID().c_str());
				ctx.scheduleForDeletion();
			}
		}

		std::erase_if(window_contexts, [](const auto& ctx) -> bool {
			return std::visit([](const auto& w) -> bool { return w.isScheduledForDeletion(); }, ctx);
		});

		ImGui::Render();
		SDL_SetRenderDrawColorFloat(app_state.renderer, background_color.x, background_color.y, background_color.z,
									background_color.w);
		SDL_RenderClear(app_state.renderer);

		if (logo_texture != nullptr) {
			const float logo_width = logo_size.x;
			const float logo_height = logo_size.y;
			const float margin = 30.0f;
			const SDL_FRect texture_rect{
				.x = io.DisplaySize.x - logo_width - margin,
				.y = io.DisplaySize.y - logo_height - margin,
				.w = logo_width,
				.h = logo_height,
			};

			SDL_RenderTexture(app_state.renderer, logo_texture, nullptr, &texture_rect); 
		}

		ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), app_state.renderer);
		SDL_RenderPresent(app_state.renderer);
	}

	// Cleanup
	ImGui_ImplSDLRenderer3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();

	SDL_DestroyRenderer(app_state.renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}
