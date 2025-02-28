#include <chrono>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <ranges>
#include <string>
#include <string_view>
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
#include "imgui_stdlib.h"
#include "implot.h"
#include "spdlog/spdlog.h"

// Own headers
#include "csv_handling.hpp"
#include "custom_type_traits.hpp"
#include "dicts.hpp"
#include "file_dialog.hpp"
#include "fonts.hpp"
#include "plotting.hpp"
#include "winapi.hpp"
#include "window_context.hpp"

extern "C" const unsigned char icon_data[];
extern "C" const size_t icon_data_size;

extern "C" const unsigned char logo_data[];
extern "C" const size_t logo_data_size;

SDL_Surface *window_icon{nullptr};

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
}  // namespace

#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
auto main(int argc, char **argv) -> int {  // NOLINT(readability-function-cognitive-complexity)
	std::set_terminate(terminateHandler);

	std::vector<std::filesystem::path> commandline_paths{};
	int max_data_points{10'000};
	bool show_debug_menu{false};

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
				show_debug_menu = true;
				spdlog::info("verbose output enabled");
			}
		} catch (const std::exception& e) {
			spdlog::critical(e.what());
			return EXIT_FAILURE;
		}

	}

	bool parallel_loading = false;
	std::chrono::time_point<std::chrono::steady_clock> loading_start_time{};
	std::chrono::time_point<std::chrono::steady_clock> loading_end_time{};

	std::list<WindowContext> window_contexts{};

	{
		const auto paths_expanded = preparePaths(commandline_paths);

		loading_start_time = std::chrono::steady_clock::now();
		if (!paths_expanded.empty()) {
			window_contexts.emplace_back(paths_expanded, loadCSVs);
		}
	}

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		spdlog::error("Error: {}", SDL_GetError());
		return -1;
	}

	spdlog::debug("SDL Initialized");
	const auto window_flags = static_cast<SDL_WindowFlags>(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
														   SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_MAXIMIZED);
	auto *window = SDL_CreateWindow("Spreadsheet Analyzer", 1280, 720, window_flags);
	auto *renderer = SDL_CreateRenderer(window, nullptr);
	SDL_SetRenderVSync(renderer, 1);

	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
	window_icon = IMG_LoadPNG_IO(SDL_IOFromMem(const_cast<unsigned char*>(icon_data), icon_data_size));
	SDL_SetWindowIcon(window, window_icon);

	SDL_ShowWindow(window);

	SDL_Texture *logo_texture{};
	ImVec2 logo_size{};
	const float logo_scale = 0.4f;
	{
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
		auto *logo = IMG_LoadPNG_IO(SDL_IOFromMem(const_cast<unsigned char *>(logo_data), logo_data_size));

		if (logo != nullptr) {
			logo_texture = SDL_CreateTextureFromSurface(renderer, logo);
			
			if (logo_texture == nullptr) {
				spdlog::error("Error: {}", SDL_GetError());
			}

			logo_size = {static_cast<float>(logo->w) * logo_scale, static_cast<float>(logo->h) * logo_scale};

			SDL_DestroySurface(logo);
		}
	}

	auto display_scale = SDL_GetWindowDisplayScale(window);
	spdlog::debug("Display scale: {}x", display_scale);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.IniFilename = nullptr;

	addFonts();

	io.FontDefault = getFont(fontList::ROBOTO_SANS_16);
	io.FontGlobalScale = display_scale;

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
	ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

	const auto background_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	// Main loop
	bool done{false};
	bool is_ctrl_pressed{false};
	bool is_shift_pressed{false};
	bool show_about{false};
	
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
					is_ctrl_pressed = true;
				}

				if ((event.key.mod & SDL_KMOD_SHIFT) != 0) {
					is_shift_pressed = true;
				}
			}

			if (event.type == SDL_EVENT_KEY_UP) {
				if ((event.key.mod & SDL_KMOD_CTRL) == 0) {
					is_ctrl_pressed = false;
				}

				if ((event.key.mod & SDL_KMOD_SHIFT) == 0) {
					is_shift_pressed = false;
				}
			}

			if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
				display_scale = SDL_GetWindowDisplayScale(window);
				io.FontGlobalScale = display_scale;
				spdlog::debug("Display scale changed to {}x", display_scale);
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

			if (ImGui::BeginMenu("Help")) {
				ImGui::MenuItem("About", nullptr, &show_about);
				ImGui::EndMenu();
			}

			if (show_debug_menu) {
				if (ImGui::BeginMenu("Debug")) {
					if (ImGui::MenuItem("Parallel loading", nullptr, &parallel_loading)) {}
					ImGui::InputInt("Max data points", &max_data_points);
					ImGui::Separator();
					const auto fps_str = fmt::format("{:.3f} ms/frame ({:.1f} FPS)", 1000.0f / ImGui::GetIO().Framerate,
													ImGui::GetIO().Framerate);
					ImGui::Text("%s", fps_str.c_str());	 // NOLINT(hicpp-vararg)
					const auto last_loading_str =
						fmt::format("Last loading took {:.3f} s",
									std::chrono::duration<double>(loading_end_time - loading_start_time).count());
					ImGui::Text("%s", last_loading_str.c_str());  // NOLINT(hicpp-vararg)
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
				loading_start_time = std::chrono::steady_clock::now();
				window_contexts.emplace_back(paths_expanded, loadCSVs);
			}
		}

		showAboutScreen(show_about, renderer);

		const auto dockspace = ImGui::DockSpaceOverViewport(ImGui::GetID("DockSpace"), ImGui::GetMainViewport(),
															ImGuiDockNodeFlags_PassthruCentralNode);

		for (auto &ctx : window_contexts) {
			ImGui::PushID(ctx.getUUID().c_str());
			ctx.checkForFinishedLoading();
			auto &dict = ctx.getData();
			auto window_open = ctx.getWindowOpenRef();
			
			ImGui::SetNextWindowDockID(dockspace, ImGuiCond_Once);
			ImGui::Begin(ctx.getWindowID().c_str(), &window_open,
						 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

			ImPlot::CreateContext();
			ImPlot::GetStyle().UseLocalTime = false;
			ImPlot::GetStyle().UseISO8601 = true;
			ImPlot::GetStyle().Use24HourClock = true;

			const auto loading_status = ctx.getLoadingStatus();

			if (loading_status.is_loading) {
				const auto progress = static_cast<float>(loading_status.finished_files) /
									  static_cast<float>(loading_status.required_files);
				const auto label = fmt::format("{:.0f}% ({}/{})", progress * 100.0f, loading_status.finished_files,
											   loading_status.required_files);
				ImGui::ProgressBar(progress, ImVec2(ImGui::GetWindowSize().x - 20, 20), label.c_str());
			} else {
				if (!dict.empty()) {
					const auto window_size = ImGui::GetWindowSize();

					ImGui::BeginChild("Column List", ImVec2(250, window_size.y - 20));
					const auto col_list_size = ImGui::GetWindowSize();

					if (ImGui::BeginListBox("List Box", ImVec2(col_list_size.x, col_list_size.y))) {
						for (auto &dct : dict) {
							if (ImGui::Selectable(dct.name.c_str(), &dct.visible)) {
								if (is_ctrl_pressed) {
									break;
								}

								if (is_shift_pressed) {
									const auto first_visible =
										std::find_if(dict.begin(), dict.end(), [](const auto &tmp) { return tmp.visible; });

									const auto current_dict = std::find_if(
										dict.begin(), dict.end(), [&dct](const auto &tmp) { return tmp.uuid == dct.uuid; });

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

					ImGui::BeginChild("File content", ImVec2(window_size.x - 255, window_size.y - 20));
					ImGui::PushFont(getFont(fontList::ROBOTO_MONO_16));
					plotDataInSubplots(dict, coerceCast<size_t>(max_data_points), ctx.getUUID());
					ImGui::PopFont();
					ImGui::EndChild();
				} else {
					ImGui::Text("No valid data found.");  // NOLINT(hicpp-vararg)
				}
			}

			ImGui::End();

			if (!window_open) {
				ctx.scheduleForDeletion();
			}

			ImGui::PopID();
		}

		window_contexts.erase(std::remove_if(window_contexts.begin(), window_contexts.end(),
											 [](const auto &ctx) { return ctx.isScheduledForDeletion(); }),
							  window_contexts.end());

		ImGui::Render();
		SDL_SetRenderDrawColorFloat(renderer, background_color.x, background_color.y, background_color.z,
									background_color.w);
		SDL_RenderClear(renderer);

		const SDL_FRect texture_rect{
			.x = io.DisplaySize.x - logo_size.x - 30.0f,
			.y = io.DisplaySize.y - logo_size.y - 30.0f,
			.w = logo_size.x,
			.h = logo_size.y
		};

		if (logo_texture != nullptr) {
			SDL_RenderTexture(renderer, logo_texture, nullptr, &texture_rect); 
		}

        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
	}

	// Cleanup
	ImGui_ImplSDLRenderer3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImPlot::DestroyContext();
	ImGui::DestroyContext();

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}