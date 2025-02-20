#include <chrono>
#include <ctime>
#include <execution>
#include <filesystem>
#include <ranges>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "SDL3/SDL_main.h"
#include "SDL3/SDL_opengl.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"
#include "csv.hpp"
#include "cxxopts.hpp"
#include "imgui.h"
#include "implot.h"
#include "nfd.hpp"
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

auto loadCSV(const std::filesystem::path &path)
	-> std::pair<std::vector<double>, std::unordered_map<std::string, data_dict>> {
	using namespace csv;

	std::vector<std::string> col_names{};
	std::vector<double> timestamp{};
	std::unordered_map<std::string, data_dict> values{};

	CSVReader reader(path.string());

	for (const auto &name : reader.get_col_names() | std::ranges::views::drop(1)) {
		if (name.empty()) {
			continue;
		}

		values[name].name = name;
		col_names.push_back(name);
	}

	for (auto &row : reader) {
		const auto date_str = row[0].get<std::string>();
		const auto date = parseDate(date_str);
		timestamp.push_back(static_cast<double>(date));

		for (const auto &col_name : col_names) {
			auto val = row[col_name].get<std::string>();
			if (val.find(',') != std::string::npos) {
				val = val.replace(val.find(','), 1, ".");
			}

			try {
				values[col_name].data.push_back(std::stod(val));
			} catch (const std::exception& /*e*/) {
				values[col_name].data.push_back(std::numeric_limits<double>::quiet_NaN());
			}
		}
	}

	return {timestamp, values};
}

auto loadCSVs(const std::vector<std::filesystem::path> &paths)
	-> std::pair<std::vector<double>, std::unordered_map<std::string, data_dict>> {
	if (paths.empty()) {
		return {{}, {}};
	}

	std::vector<double> timestamp{};
	std::unordered_map<std::string, data_dict> values{};

	struct context {
		size_t index;
		std::filesystem::path path;
		std::vector<double> timestamp{};
		std::unordered_map<std::string, data_dict> values{};
	};

	std::vector<context> contexts{};
	contexts.reserve(paths.size());

	for (size_t i = 0; const auto& path : paths) {
		contexts.push_back({.index = ++i, .path = path});
	}

	std::for_each(std::execution::par_unseq, contexts.begin(), contexts.end(), [&contexts](auto &ctx) {
		spdlog::info("Loading file: {} ({}/{})...", ctx.path.filename().string(), ctx.index, contexts.size());
		const auto [ts, val] = loadCSV(ctx.path);
		ctx.timestamp = ts;
		ctx.values = val;
	});

	spdlog::info("Merging data...");

	for (const auto &ctx : contexts) {
		timestamp.insert(timestamp.end(), ctx.timestamp.begin(), ctx.timestamp.end());

		for (const auto &[key, value] : ctx.values) {
			if (values.find(key) == values.end()) {
				values[key] = value;
			} else {
				values[key].data.insert(values[key].data.end(), value.data.begin(), value.data.end());
			}
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

auto selectFilesFromDialog() -> std::vector<std::filesystem::path> {
	NFD::Guard nfdGuard;
	NFD::UniquePathSet outPaths;

	const auto filters = std::array<nfdfilteritem_t, 1>{
		nfdfilteritem_t{"CSV", "csv"}
	};

	const auto result = NFD::OpenDialogMultiple(outPaths, filters.data(), filters.size());

	if (result == NFD_OKAY) {
		nfdpathsetsize_t numPaths;
		NFD::PathSet::Count(outPaths, numPaths);
		std::vector<std::filesystem::path> paths{};
		paths.reserve(numPaths);

		for (nfdpathsetsize_t i = 0; i < numPaths; ++i) {
			NFD::UniquePathSetPath path;
			NFD::PathSet::GetPath(outPaths, i, path);
			paths.push_back(std::filesystem::path(path.get()));
		}

		return paths;
	} else if (result == NFD_CANCEL) {
		spdlog::info("User pressed cancel.");
	} else {
		spdlog::error("Error: {}", NFD::GetError());
	}

	return {};
}

// #pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
auto main(int argc, char ** argv) -> int {
	std::vector<std::filesystem::path> paths{};
	
	{
		cxxopts::Options options(argv[0], "ImPlot Demo using ImGui and SDL2");

		options.add_options()
			("h,help", "Print usage")
			("filename", "CSV file to load", cxxopts::value<std::vector<std::string>>(), "FILE")
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
					paths.push_back(std::filesystem::path(file));
				}
			}
		} catch (const std::exception& e) {
			spdlog::critical(e.what());
			return EXIT_FAILURE;
		}

	}

	if (paths.empty()) {
		paths = selectFilesFromDialog();
	}

	if (paths.empty()) {
		spdlog::error("No files selected.");
		return EXIT_FAILURE;
	}

	std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) {
		return a.filename() < b.filename();
	});

	const auto [x, y] = loadCSVs(paths);

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		spdlog::error("Error: {}", SDL_GetError());
		return -1;
	}

	spdlog::info("SDL Initialized");

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
	SDL_Window *window = SDL_CreateWindow("Spreadsheet Analyzer 2.0", 1280, 720, window_flags);
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
			ImGui::Begin("File content", &show_plot_window, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);

			if (ImGui::BeginTabBar("ImPlotDemoTabs")) {
				for (const auto &dct : y) {
					if (ImGui::BeginTabItem(dct.first.c_str())) {
						Demo_LinePlots(x, dct.second);
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