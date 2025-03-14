#include <chrono>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <list>
#include <ranges>
#include <span>
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
#include "raw_handling.hpp"
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
				spdlog::critical("\twith `what()` = \"{}\"", e.what());
			} catch (...) {}  // NOLINT(bugprone-empty-catch)
		} else {
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

	enum class agg_id_t : uint8_t {
		mean,
		stdevp,
		stdevm
	};

	struct plot_data_t {
		const std::vector<downsampled_data_t>* data;
		raw_stream_t stream_id;
		agg_id_t agg_id;
	};

	auto plotRaw(int i, void *data) -> ImPlotPoint {
		assert(i >= 0);
		assert(data != nullptr);

		const auto &plot_data = *static_cast<plot_data_t*>(data);
		const auto &data_set = *plot_data.data;

		auto get_agg = [&plot_data](const auto &entry) -> double {
			switch (plot_data.agg_id) {
				case agg_id_t::mean:
					return static_cast<double>(entry.mean);
				case agg_id_t::stdevp:
					return static_cast<double>(entry.mean + entry.stddev);
				case agg_id_t::stdevm:
					return static_cast<double>(entry.mean - entry.stddev);
				default: break;
			}

			return std::numeric_limits<double>::quiet_NaN();
		};

		try {
			const auto &date = data_set.at(static_cast<size_t>(i)).date;

			switch (plot_data.stream_id) {
				case raw_stream_t::AMPLITUDE:
					return {date, get_agg(data_set.at(static_cast<size_t>(i)).amplitude)};
				case raw_stream_t::RUNTIME:
					return {date, get_agg(data_set.at(static_cast<size_t>(i)).runtime)};
				case raw_stream_t::COE:
					return {date, get_agg(data_set.at(static_cast<size_t>(i)).coe)};
				case raw_stream_t::INT1:
					return {date, get_agg(data_set.at(static_cast<size_t>(i)).int1)};
				case raw_stream_t::INT2:
					return {date, get_agg(data_set.at(static_cast<size_t>(i)).int2)};
				default: break;
			}
		} catch (const std::exception &e) {}

		return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
	}

	auto setSystemLocale() -> void {
		try {
			const auto locale = std::locale("");
			std::locale::global(locale);
		} catch (const std::exception &e) {
			spdlog::warn("Error setting system locale: {}", e.what());
		}
	}
}  // namespace

#if defined(_WIN32)
#ifndef DEBUG
// #pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
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

			if (result.count("help") != 0u) {
				spdlog::info(options.help());
				return EXIT_SUCCESS;
			}

			if (result.count("filename") > 0u) {
				for (const auto& file : result["filename"].as<std::vector<std::string>>()) {
					commandline_paths.push_back(std::filesystem::path(file));
				}
			}

			if (result.count("verbose") == 1u) {
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

		std::vector<std::filesystem::path> csv_paths;
		for (const auto& path : paths_expanded) {
			if (path.extension() == ".csv") {
				csv_paths.push_back(path);
			}
		}

		if (!csv_paths.empty()) {
			window_contexts.emplace_back(std::in_place_type<CSVWindowContext>, csv_paths, loadCSVs);
		}
	}

	const auto raw_data = [&commandline_paths]() -> std::vector<downsampled_data_t> {
		std::vector<downsampled_data_t> raw_data{};
		std::vector<std::filesystem::path> raw_stream_paths;

		for (const auto &path : commandline_paths) {
			if (path.extension() == ".bin") {
				raw_stream_paths.push_back(path);
			}
		}

		if (!raw_stream_paths.empty()) {
			const auto raw_files = parseRawFiles(raw_stream_paths);
			raw_data = getDownsampled(std::span{raw_files.getEntries()}, {raw_stream_t::COE, raw_stream_t::INT1, raw_stream_t::INT2},
									  raw_files.getEntries().size());
		}

		return raw_data;
	}();

	if (!raw_data.empty()) {
		window_contexts.emplace_back(std::in_place_type<RawWindowContext>, raw_data);
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
	spdlog::debug("Display scale: {}x", app_state.display_scale);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
	io.IniFilename = nullptr;

	addFonts();

	io.FontDefault = getFont(fontList::ROBOTO_SANS_16);
	io.FontGlobalScale = app_state.display_scale;

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
				io.FontGlobalScale = app_state.display_scale;
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
				
				std::vector<std::filesystem::path> csv_paths;
				for (const auto& path : paths_expanded) {
					if (path.extension() == ".csv") {
						csv_paths.push_back(path);
					}
				}

				if (!csv_paths.empty()) {
					window_contexts.emplace_back(std::in_place_type<CSVWindowContext>, csv_paths, loadCSVs);
				}
			}
		}

		showAboutScreen();

		const auto dockspace = ImGui::DockSpaceOverViewport(ImGui::GetID("DockSpace"), ImGui::GetMainViewport(),
															ImGuiDockNodeFlags_PassthruCentralNode);
		
		if (app_state.show_debug_menu) {
			ImGui::ShowMetricsWindow();
		}

		updateDateRange(window_contexts);

		for (auto &temp : window_contexts) {
			if (!std::holds_alternative<RawWindowContext>(temp)) {
				continue;
			}

			auto &ctx = std::get<RawWindowContext>(temp);
			auto &window_open = ctx.getWindowOpenRef();

			ImGui::SetNextWindowDockID(dockspace, ImGuiCond_Once);
			if (ImGui::Begin("test", &window_open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
				ctx.switchToImPlotContext();

				const auto window_content_size = ImGui::GetContentRegionAvail();
				if (ImPlot::BeginPlot("test", window_content_size, ImPlotFlags_NoTitle)) {
					ImPlot::SetupAxis(ImAxis_X1, "date");
					ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
					ImPlot::SetupAxisZoomConstraints(ImAxis_X1, 0, 120.0);
					ImPlot::SetupAxis(ImAxis_Y1, "Int1");
					ImPlot::SetupAxis(ImAxis_Y2, "Int2");
					ImPlot::SetupAxis(ImAxis_Y3, "CoE");

					ImPlot::SetAxis(ImAxis_Y1);

					const auto &data = ctx.getData();

					plot_data_t plot_data_mean{
						.data = &data, .stream_id = raw_stream_t::INT1, .agg_id = agg_id_t::mean};

					plot_data_t plot_data_stdevp{
						.data = &data, .stream_id = raw_stream_t::INT1, .agg_id = agg_id_t::stdevp};

					plot_data_t plot_data_stdevm{
						.data = &data, .stream_id = raw_stream_t::INT1, .agg_id = agg_id_t::stdevm};

					ImPlot::PlotLineG("Int1", plotRaw, &plot_data_mean, static_cast<int>(data.size()));
					ImPlot::SetNextFillStyle(ImPlot::GetLastItemColor(), 0.25f);
					ImPlot::PlotShadedG("##Int1_shaded", plotRaw, &plot_data_stdevp, plotRaw, &plot_data_stdevm,
										static_cast<int>(data.size()));

					ImPlot::SetAxis(ImAxis_Y2);

					plot_data_mean.stream_id = raw_stream_t::INT2;
					plot_data_stdevp.stream_id = raw_stream_t::INT2;
					plot_data_stdevm.stream_id = raw_stream_t::INT2;

					ImPlot::PlotLineG("Int2", plotRaw, &plot_data_mean, static_cast<int>(data.size()));
					ImPlot::SetNextFillStyle(ImPlot::GetLastItemColor(), 0.25f);
					ImPlot::PlotShadedG("##Int2_shaded", plotRaw, &plot_data_stdevp, plotRaw, &plot_data_stdevm,
										static_cast<int>(data.size()));

					ImPlot::SetAxis(ImAxis_Y3);

					plot_data_mean.stream_id = raw_stream_t::COE;
					plot_data_stdevp.stream_id = raw_stream_t::COE;
					plot_data_stdevm.stream_id = raw_stream_t::COE;

					ImPlot::PlotLineG("CoE", plotRaw, &plot_data_mean, static_cast<int>(data.size()));
					ImPlot::SetNextFillStyle(ImPlot::GetLastItemColor(), 0.25f);
					ImPlot::PlotShadedG("##CoE_shaded", plotRaw, &plot_data_stdevp, plotRaw, &plot_data_stdevm,
										static_cast<int>(data.size()));

					ImPlot::EndPlot();
				}
			}
			ImGui::End();

			if (!window_open) {
				ImGui::ClearWindowSettings(ctx.getWindowID().c_str());
				ctx.scheduleForDeletion();
			}
		}

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
						ImGui::SetTooltip("Unlink x-axes");
					} else {
						ImGui::SetTooltip("Link x-axes");
					}
				}

				bool &force_subplot = ctx.getForceSubplotRef();

				ImGui::MenuItem(ICON_FA_TABLE_LIST, nullptr, &force_subplot);
				
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
					ImGui::SetTooltip("Force subplots");
				}

				if (ImGui::MenuItem(ICON_FA_CLONE, nullptr, nullptr, !loading_status.is_loading)) {
					window_contexts.push_back(ctx);
				}

				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
					ImGui::SetTooltip("Duplicate");
				}
				ImGui::EndMenuBar();
			}

			const auto window_content_size = ImGui::GetContentRegionAvail();

			if (loading_status.is_loading) {
				const auto progress = static_cast<float>(loading_status.finished_files) /
									  static_cast<float>(loading_status.required_files);
				const auto label = fmt::format("{:.0f}% ({}/{})", progress * 100.0f, loading_status.finished_files,
											   loading_status.required_files);

				const auto padding = std::min(100.0f, window_content_size.x / 10.0f);
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padding);
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + window_content_size.y / 2.0f - 10.0f);
				ImGui::ProgressBar(progress, ImVec2(window_content_size.x - 2.0f * padding, 20.0f), label.c_str());
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
									const auto first_visible = std::find_if(
										dict.begin(), dict.end(), [](const auto &tmp) { return tmp.visible; });

									const auto current_dict =
										std::find_if(dict.begin(), dict.end(),
													 [&dct](const auto &tmp) { return tmp.uuid == dct.uuid; });

									if (first_visible != dict.end() && current_dict != dict.end()) {
										const auto first_index = std::distance(dict.begin(), first_visible);
										const auto current_index = std::distance(dict.begin(), current_dict);

										const auto start = std::min(first_index, current_index);
										const auto stop = std::max(first_index, current_index);

										std::for_each(dict.begin() + start, dict.begin() + stop + 1,
													  [](auto &tmp) { tmp.visible = true; });
									}

									break;
								}

								std::for_each(dict.begin(), dict.end(), [](auto &tmp) { tmp.visible = false; });
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

		window_contexts.erase(std::remove_if(window_contexts.begin(), window_contexts.end(),
											 [](const auto &ctx) {
												 return std::visit(
													 [](const auto &w) { return w.isScheduledForDeletion(); }, ctx);
											 }),
							  window_contexts.end());

		ImGui::Render();
		SDL_SetRenderDrawColorFloat(app_state.renderer, background_color.x, background_color.y, background_color.z,
									background_color.w);
		SDL_RenderClear(app_state.renderer);

		const SDL_FRect texture_rect{
			.x = io.DisplaySize.x - (logo_size.x * app_state.display_scale) - (30.0f * app_state.display_scale),
			.y = io.DisplaySize.y - (logo_size.y * app_state.display_scale) - (30.0f * app_state.display_scale),
			.w = logo_size.x * app_state.display_scale,
			.h = logo_size.y * app_state.display_scale
		};

		if (logo_texture != nullptr) {
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
