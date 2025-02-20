#include <chrono>
#include <ctime>
#include <filesystem>
#include <ranges>
#include <string_view>
#include <vector>
#include <sstream>

#include "SDL3/SDL_main.h"
#include "SDL3/SDL_opengl.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"
#include "csv.hpp"
#include "cxxopts.hpp"
#include "imgui.h"
#include "implot.h"
#include "spdlog/spdlog.h"


struct data_dict {
	std::string name;
	std::string unit;

	std::vector<double> data{};
};

auto parseDate(const std::string& str) -> time_t {
	std::tm t{};
	std::istringstream ss(str);
	ss.imbue(std::locale("de_DE.utf-8"));
	ss >> std::get_time(&t, "%Y/%m/%d %H:%M:%S");
	return std::mktime(&t);
}

auto loadCSV(std::filesystem::path path) -> std::pair<std::vector<double>, std::vector<data_dict>> {
	using namespace csv;
	CSVReader reader(path.string());

	std::vector<double> timestamp{};
	std::vector<data_dict> values{};

	for (const auto& name : reader.get_col_names() | std::ranges::views::drop(1)) {
		if (name.empty()) {
			continue;
		}

		values.push_back({name, ""});
	}

	for (auto &row : reader) {
		const auto date_str = row[0].get<std::string>();
		const auto date = parseDate(date_str);
		timestamp.push_back(static_cast<double>(date));

		for (auto& dct : values) {
			auto val = row[dct.name].get<std::string>();
			if (val.find(',') != std::string::npos) {
				val = val.replace(val.find(','), 1, ".");
			}

			dct.data.push_back(std::stof(val));
		}
	}

	return {timestamp, values};
}

void Demo_LinePlots(const std::vector<double> &timestamp, const data_dict &y) {
	ImVec2 vMin = ImGui::GetWindowContentRegionMin();
	ImVec2 vMax = ImGui::GetWindowContentRegionMax();

	vMin.x += ImGui::GetWindowPos().x;
	vMin.y += ImGui::GetWindowPos().y;
	vMax.x += ImGui::GetWindowPos().x;
	vMax.y += ImGui::GetWindowPos().y;

	if (ImPlot::BeginPlot(y.name.c_str(), ImVec2(vMax.x - vMin.x, (vMax.y - vMin.y) * 0.95), ImPlotFlags_NoLegend | ImPlotFlags_NoTitle)) {
		ImPlot::SetupAxes("date", y.name.c_str());
		ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
		ImPlot::PlotLine(y.name.c_str(), timestamp.data(), y.data.data(), timestamp.size());
		ImPlot::EndPlot();
	}
}

auto main(int argc, char ** argv) -> int {
	std::filesystem::path path{};
	{
		cxxopts::Options options(argv[0], "ImPlot Demo using ImGui and SDL2");

		options.add_options()
			("h,help", "Print usage")
			("filename", "CSV file to load", cxxopts::value<std::string>(), "FILE")
			;

		try {
			options.parse_positional({"filename"});
			auto result = options.parse(argc, argv);

			if (result.count("help") != 0u) {
				spdlog::info(options.help());
				return EXIT_SUCCESS;
			}

			if (result.count("filename") == 0u) {
				spdlog::error("Filename is required");
				return EXIT_FAILURE;
			}else {
				path = result["filename"].as<std::string>();
			}
		} catch (const std::exception& e) {
			spdlog::critical(e.what());
			return EXIT_FAILURE;
		}

	}

	const auto [x, y] = loadCSV(path);

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		spdlog::error("Error: {}", SDL_GetError());
		return -1;
	}

	spdlog::info("SDL Initialized");
	const auto filename = path.filename().string();

	const char *glsl_version = "#version 130";
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

	// Create window with graphics context
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_WindowFlags window_flags =
		(SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	SDL_Window *window = SDL_CreateWindow(filename.c_str(), 1280, 720, window_flags);
	SDL_GLContext gl_context = SDL_GL_CreateContext(window);
	SDL_GL_MakeCurrent(window, gl_context);
	SDL_GL_SetSwapInterval(1);	// Enable vsync

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	// ImGui::StyleColorsClassic();

	// Setup Platform/Renderer backends
	ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
	ImGui_ImplOpenGL3_Init(glsl_version);

	bool show_plot_window = false;
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	// Main loop
	bool done = false;
	while (!done) {
		// Poll and handle events (inputs, window resize, etc.)
		// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to
		// tell if dear imgui wants to use your inputs.
		// - When io.WantCaptureMouse is true, do not dispatch mouse input data
		// to your main application, or clear/overwrite your copy of the mouse
		// data.
		// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input
		// data to your main application, or clear/overwrite your copy of the
		// keyboard data. Generally you may always pass all inputs to dear
		// imgui, and hide them from your application based on those two flags.
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSDL3_ProcessEvent(&event);
			if (event.type == SDL_EVENT_QUIT) {
				done = true;
			}
			if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) {
				done = true;
			}
		}

		// Start the Dear ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();
		ImGui::GetStyle().WindowRounding = 0.0f;

		{
			ImPlot::CreateContext();
			ImPlot::GetStyle().UseLocalTime = true;
			ImPlot::GetStyle().UseISO8601 = true;
			ImPlot::GetStyle().Use24HourClock = true;

			ImGui::SetNextWindowPos(ImVec2(0, 0));
			ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y));
			ImGui::Begin("File content", &show_plot_window);

			if (ImGui::BeginTabBar("ImPlotDemoTabs")) {
				for (const auto &dct : y) {
					if (ImGui::BeginTabItem(dct.name.c_str())) {
						Demo_LinePlots(x, dct);
						ImGui::EndTabItem();
					}
				}

				ImGui::EndTabBar();
			}

			ImGui::End();
		}

		// Rendering
		ImGui::Render();
		glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
		glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w,
					 clear_color.w);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(window);
	}

	// Cleanup
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImPlot::DestroyContext();
	ImGui::DestroyContext();

	SDL_GL_DestroyContext(gl_context);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}