#include <chrono>
#include <cstdlib>
#include <ctime>
#include <exception>
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

extern "C" const unsigned int font_fira_code_compressed_size;
extern "C" const unsigned char font_fira_code_compressed_data[];

namespace {
	struct data_dict {
		std::string name;
		std::string unit;
	
		std::vector<time_t> timestamp{};
		std::vector<double> data{};
	};

	struct immediate_dict {
		std::string name;
		std::string unit;

		std::vector<std::pair<time_t, double>> data{};
	};

	auto parseDate(const std::string &str) -> time_t {
		std::tm t{};
		std::istringstream ss(str);
		ss.imbue(std::locale("de_DE.utf-8"));
		ss >> std::get_time(&t, "%Y/%m/%d %H:%M:%S");
		return std::mktime(&t);
	}

	auto stripUnit(std::string_view header) -> std::pair<std::string, std::string> {
		const auto pos = header.find_last_of('(');
		if (pos == std::string::npos) {
			return {std::string(header), ""};
		}

		const auto name = header.substr(0, pos);
		const auto unit = header.substr(pos + 1, header.find_last_of(')') - pos - 1);

		if (unit.size() > 5){
			return {std::string(header), ""};
		}

		return {std::string(name), std::string(unit)};
	}

	auto loadCSV(const std::filesystem::path &path) -> std::unordered_map<std::string, immediate_dict> {
		using namespace csv;

		std::vector<std::string> col_names{};
		std::unordered_map<std::string, immediate_dict> values{};

		CSVReader reader(path.string());

		for (const auto &header_string : reader.get_col_names() | std::ranges::views::drop(1)) {
			if (header_string.empty()) {
				continue;
			}

			const auto [name, unit] = stripUnit(header_string);

			values[header_string] = {.name = name, .unit = unit, .data = {}};
			col_names.push_back(header_string);
		}

		for (auto &row : reader) {
			const auto date_str = row[0].get<std::string>();
			const auto date = parseDate(date_str);

			for (const auto &col_name : col_names) {
				try {
					auto val = row[col_name].get<std::string>();

					const auto pos = val.find(',');
					if (pos != std::string::npos) {
						val = val.replace(pos, 1, ".");
					}

					values[col_name].data.push_back({date, std::stod(val)});
				} catch (const std::exception & /*e*/) {}
			}
		}

		return values;
	}

	auto loadCSVs(const std::vector<std::filesystem::path> &paths) -> std::vector<data_dict> {
		if (paths.empty()) {
			return {};
		}

		std::unordered_map<std::string, immediate_dict> values_temp{};
		std::vector<data_dict> values{};

		struct context {
			size_t index;
			std::filesystem::path path;
			std::unordered_map<std::string, immediate_dict> values{};
		};

		std::vector<context> contexts{};
		contexts.reserve(paths.size());

		for (size_t i = 0; const auto &path : paths) {
			contexts.push_back({.index = ++i, .path = path});
		}

		std::for_each(std::execution::par_unseq, contexts.begin(), contexts.end(), [&contexts](auto &ctx) {
			spdlog::info("Loading file: {} ({}/{})...", ctx.path.filename().string(), ctx.index, contexts.size());
			ctx.values = loadCSV(ctx.path);
		});

		spdlog::info("Merging data...");

		for (const auto &ctx : contexts) {
			for (const auto &[key, value] : ctx.values) {
				if (value.data.empty()) {
					continue;
				}
				
				if (values_temp.find(key) == values_temp.end()) {
					values_temp[key] = value;
				} else {
					values_temp[key].data.insert(values_temp[key].data.end(), value.data.begin(), value.data.end());
				}
			}
		}

		values.reserve(values_temp.size());

		for (auto &&[key, value] : values_temp) {
			std::sort(std::execution::par, value.data.begin(), value.data.end(),
					  [](const auto &a, const auto &b) { return a.first < b.first; });

			data_dict dd{};
			dd.name = value.name;
			dd.unit = value.unit;

			for (const auto &[date, val] : value.data) {
				dd.timestamp.push_back(date);
				dd.data.push_back(val);
			}

			values.push_back(dd);
		}

		return values;
	}

	auto plotDict(int i, void *data) -> ImPlotPoint {
		const auto &dd = *static_cast<data_dict *>(data);
		return ImPlotPoint(static_cast<double>(dd.timestamp[i]), dd.data[i]);
	}

	auto plotColumn(const data_dict &col) -> void {
		ImVec2 v_min = ImGui::GetWindowContentRegionMin();
		ImVec2 v_max = ImGui::GetWindowContentRegionMax();
	
		v_min.x += ImGui::GetWindowPos().x;
		v_min.y += ImGui::GetWindowPos().y;
		v_max.x += ImGui::GetWindowPos().x;
		v_max.y += ImGui::GetWindowPos().y;
	
		if (ImPlot::BeginPlot(col.name.c_str(), ImVec2(v_max.x - v_min.x, (v_max.y - v_min.y) * 0.95f),
							  ImPlotFlags_NoLegend | ImPlotFlags_NoTitle)) {
			const auto fmt = [&col]() -> std::string {
				if (col.unit.empty()) {
					return "%g";
				}
				
				if (col.unit == "%") {
					return "%g%%";
				}

				return "%g " + col.unit;
			}();

			ImPlot::SetupAxes("date", col.name.c_str());
			ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
			ImPlot::SetupAxisFormat(ImAxis_Y1, fmt.c_str());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
			auto *data = const_cast<void *>(static_cast<const void *>(&col));
			ImPlot::PlotLineG(col.name.c_str(), plotDict, data, col.timestamp.size());
			ImPlot::EndPlot();
		}
	}

	auto selectFilesFromDialog() -> std::vector<std::filesystem::path> {
		const NFD::Guard nfd_guard{};
		NFD::UniquePathSet out_paths{};
	
		const auto filters = std::array<nfdfilteritem_t, 1>{
			nfdfilteritem_t{"CSV", "csv"}
		};
	
		const auto result = NFD::OpenDialogMultiple(out_paths, filters.data(), filters.size());
	
		if (result == NFD_OKAY) {
			nfdpathsetsize_t num_paths{};
			NFD::PathSet::Count(out_paths, num_paths);
			std::vector<std::filesystem::path> paths{};
			paths.reserve(num_paths);
	
			for (nfdpathsetsize_t i = 0; i < num_paths; ++i) {
				NFD::UniquePathSetPath path;
				NFD::PathSet::GetPath(out_paths, i, path);
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

// #pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
auto main(int argc, char ** argv) -> int {
	std::set_terminate(terminateHandler);

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

	std::vector<std::filesystem::path> new_paths{};

	for (auto& path : paths) {
		if (std::filesystem::is_directory(path)) {
			for (const auto& entry : std::filesystem::directory_iterator(path)) {
				if (entry.path().extension() == ".csv") {
					new_paths.push_back(entry.path());
				}
			}
		} else {
			new_paths.push_back(path);
		}
	}

	const auto data_dict = loadCSVs(new_paths);

	if (data_dict.empty()) {
		spdlog::error("No valid data found.");
		return EXIT_FAILURE;
	}

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
	const auto window_flags =
		static_cast<SDL_WindowFlags>(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	SDL_Window *window = SDL_CreateWindow("Spreadsheet Analyzer 2.0", 1280, 720, window_flags);
	SDL_GLContext gl_context = SDL_GL_CreateContext(window);
	SDL_GL_MakeCurrent(window, gl_context);
	SDL_GL_SetSwapInterval(1);	// Enable vsync

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.Fonts->AddFontFromMemoryCompressedTTF(static_cast<const void *>(font_fira_code_compressed_data),
											 static_cast<int>(font_fira_code_compressed_size), 15.0f);

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	// ImGui::StyleColorsClassic();

	// Setup Platform/Renderer backends
	ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
	ImGui_ImplOpenGL3_Init(glsl_version);

	bool show_plot_window = false;
	const auto clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

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
				for (const auto &dct : data_dict) {
					if (ImGui::BeginTabItem(dct.name.c_str())) {
						plotColumn(dct);
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