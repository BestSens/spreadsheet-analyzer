#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <execution>
#include <filesystem>
#include <future>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#include "SDL3/SDL_main.h"
#include "SDL3/SDL_opengl.h"
#pragma clang diagnostic pop
#include "SDL3_image/SDL_image.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include "csv.hpp"
#include "cxxopts.hpp"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "implot.h"
#include "nfd.hpp"
#include "spdlog/spdlog.h"
#include "winapi.hpp"

extern "C" const unsigned int font_roboto_mono_compressed_size;
extern "C" const unsigned char font_roboto_mono_compressed_data[];
extern "C" const unsigned int font_roboto_sans_compressed_size;
extern "C" const unsigned char font_roboto_sans_compressed_data[];

extern "C" const unsigned char icon_data[];
extern "C" const size_t icon_data_size;

extern "C" const unsigned char logo_data[];
extern "C" const size_t logo_data_size;

namespace {
	struct data_dict_t {
		std::string name;
		std::string unit;
		bool visible{false};
	
		std::vector<time_t> timestamp{};
		std::vector<double> data{};
	};

	struct immediate_dict {
		std::string name;
		std::string unit;

		std::vector<std::pair<time_t, double>> data{};
	};

	auto parseDate(const std::string &str) -> time_t {
		std::istringstream ss(str);
		ss.imbue(std::locale("de_DE.utf-8"));
		
		std::chrono::sys_seconds tp{};
		ss >> std::chrono::parse("%Y/%m/%d %H:%M:%S", tp);
		
		return std::chrono::system_clock::to_time_t(tp);
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

	auto loadCSV(const std::filesystem::path &path, const std::atomic<bool> &stop_loading)
		-> std::unordered_map<std::string, immediate_dict> {
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

					const auto dbl_val = std::stod(val);

					if (std::isfinite(dbl_val)) {
						values[col_name].data.push_back({date, dbl_val});
					}
				} catch (const std::exception & /*e*/) {}

				if (stop_loading) {
					break;
				}
			}

			if (stop_loading) {
				break;
			}
		}

		return values;
	}

	auto loadCSVs(const std::vector<std::filesystem::path> &paths, std::atomic<size_t> &finished,
				  std::atomic<bool> &stop_loading, int max_data_points, std::atomic<size_t> &max_loaded_data_points,
				  bool parallel_loading) -> std::vector<data_dict_t> {
		if (paths.empty()) {
			return {};
		}

		std::unordered_map<std::string, immediate_dict> values_temp{};
		std::vector<data_dict_t> values{};

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

		auto fn = [&contexts, &stop_loading, &finished](auto &ctx) {
			if (!stop_loading) {
				spdlog::info("Loading file: {} ({}/{})...", ctx.path.filename().string(), ctx.index,
							 contexts.size());
				try {
					ctx.values = loadCSV(ctx.path, stop_loading);
				} catch (const std::exception &e) {
					spdlog::error("{}", e.what());
				}
				++finished;
			}
		};

		if (parallel_loading) {
			std::for_each(std::execution::par, contexts.begin(), contexts.end(), fn);
		} else {
			std::for_each(std::execution::seq, contexts.begin(), contexts.end(), fn);
		}

		if (stop_loading) {
			return {};
		}

		spdlog::debug("Merging data...");

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

			data_dict_t dd{};
			dd.name = value.name;
			dd.unit = value.unit;

			for (const auto &[date, val] : value.data) {
				dd.timestamp.push_back(date);
				dd.data.push_back(val);
			}

			values.push_back(dd);
		}

		std::for_each(
			std::execution::par_unseq, values.begin(), values.end(),
			[&max_loaded_data_points, max_data_points](auto &dd) {
				const auto reduction_factor =
					static_cast<size_t>(std::ceil(static_cast<double>(dd.timestamp.size()) / max_data_points));

				if (reduction_factor > 1) {
					std::vector<time_t> timestamp_lowres{};
					timestamp_lowres.reserve(max_data_points);
					std::vector<double> data_lowres{};
					data_lowres.reserve(max_data_points);

					for (size_t i = 0; i < dd.data.size(); i += reduction_factor) {
						timestamp_lowres.push_back(dd.timestamp[i]);
						data_lowres.push_back(dd.data[i]);
					}

					dd.timestamp = std::move(timestamp_lowres);
					dd.data = std::move(data_lowres);
				}

				if (dd.timestamp.size() > max_loaded_data_points) {
					max_loaded_data_points.store(dd.timestamp.size());
				}
			});

		return values;
	}

	auto plotDict(int i, void *data) -> ImPlotPoint {
		const auto &dd = *static_cast<data_dict_t *>(data);
		return ImPlotPoint(static_cast<double>(dd.timestamp[i]), dd.data[i]);
	}

	auto plotDataInSubplots(const std::vector<data_dict_t> &data) -> void {
		ImVec2 v_min = ImGui::GetWindowContentRegionMin();
		ImVec2 v_max = ImGui::GetWindowContentRegionMax();

		v_min.x += ImGui::GetWindowPos().x;
		v_min.y += ImGui::GetWindowPos().y;
		v_max.x += ImGui::GetWindowPos().x;
		v_max.y += ImGui::GetWindowPos().y;

		const auto plot_height = v_max.y - v_min.y;
		const auto plot_width = v_max.x - v_min.x;

		const auto n_selected =
			std::count_if(data.begin(), data.end(), [](const auto &dct) { return dct.visible; });

		if (n_selected == 0) {
			return;
		}

		// time_t date_max = std::numeric_limits<time_t>::lowest();
		// time_t date_min = std::numeric_limits<time_t>::max();

		// for (const auto &col : data) {
		// 	if (!col.visible) {
		// 		continue;
		// 	}

		// 	if (col.timestamp.empty()) {
		// 		continue;
		// 	}

		// 	if (col.timestamp.at(0) < date_min) {
		// 		date_min = col.timestamp.at(0);
		// 	}

		// 	if (col.timestamp.at(col.timestamp.size() - 1) > date_max) {
		// 		date_max = col.timestamp.at(col.timestamp.size() - 1);
		// 	}
		// }

		const auto [rows, cols] = [n_selected]() -> std::pair<size_t, size_t> {
			if (n_selected <= 3) {
				return {n_selected, 1};
			}

			return {(n_selected + 1) / 2, 2};
		}();

		if (ImPlot::BeginSubplots("", rows, cols, ImVec2(plot_width, plot_height))) {
			for (const auto &col : data) {
				if (!col.visible) {
					continue;
				}

				if (ImPlot::BeginPlot(col.name.c_str(), ImVec2(plot_width, plot_height),
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

			ImPlot::EndSubplots();
		}
	}

	auto selectFilesFromDialog(bool select_folder) -> std::vector<std::filesystem::path> {
		const NFD::Guard nfd_guard{};
		NFD::UniquePathSet out_paths{};
	
		const auto filters = std::array<nfdfilteritem_t, 1>{
			nfdfilteritem_t{"CSV", "csv"}
		};

		const auto result = [&]() {
			if (!select_folder) {
				return NFD::OpenDialogMultiple(out_paths, filters.data(), filters.size());
			} else {
				const nfdu8char_t *default_path = nullptr;
				return NFD::PickFolderMultiple(out_paths, default_path);
			}
		}();

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
			spdlog::debug("User pressed cancel.");
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

	auto preparePaths(std::vector<std::filesystem::path> paths) -> std::vector<std::filesystem::path> {
		std::vector<std::filesystem::path> files{};
		files.reserve(paths.size());

		std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) {
			return a.filename() < b.filename();
		});

		for (auto& path : paths) {
			if (std::filesystem::is_directory(path)) {
				for (const auto& entry : std::filesystem::directory_iterator(path)) {
					if (entry.path().extension() == ".csv") {
						files.push_back(entry.path());
					}
				}
			} else {
				files.push_back(path);
			}
		}

		return files;
	}
}  // namespace

#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
auto main(int argc, char **argv) -> int {  // NOLINT(readability-function-cognitive-complexity)
	std::set_terminate(terminateHandler);

	std::vector<std::filesystem::path> paths{};
	int max_data_points = 1'000'000;
	
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
					paths.push_back(std::filesystem::path(file));
				}
			}

			if (result.count("verbose") == 1u) {
				spdlog::set_level(spdlog::level::debug);
				spdlog::info("verbose output enabled");
			}
		} catch (const std::exception& e) {
			spdlog::critical(e.what());
			return EXIT_FAILURE;
		}

	}

	size_t required_files{0};
	std::atomic<size_t> finished_files{0};
	std::atomic<size_t> max_loaded_data_points{0};
	size_t max_loaded_data_points_current{0};
	std::atomic<bool> stop_loading{false};
	bool parallel_loading = false;
	std::chrono::time_point<std::chrono::steady_clock> loading_start_time{};
	std::chrono::time_point<std::chrono::steady_clock> loading_end_time{};

	auto paths_expanded = preparePaths(paths);
	required_files = paths_expanded.size();

	loading_start_time = std::chrono::steady_clock::now();
	std::future<std::vector<data_dict_t>> data_dict_f = std::async(
		std::launch::async,
		[paths_expanded, &finished_files, &stop_loading, max_data_points, &max_loaded_data_points, parallel_loading] {
			return loadCSVs(paths_expanded, finished_files, stop_loading, max_data_points, max_loaded_data_points,
							parallel_loading);
		});

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		spdlog::error("Error: {}", SDL_GetError());
		return -1;
	}

	spdlog::debug("SDL Initialized");
	const auto window_flags = static_cast<SDL_WindowFlags>(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
														   SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_MAXIMIZED);
	auto *window = SDL_CreateWindow("Spreadsheet Analyzer 2.0", 1280, 720, window_flags);
	auto *renderer = SDL_CreateRenderer(window, nullptr);
	SDL_SetRenderVSync(renderer, 1);

	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
	auto* window_icon = IMG_LoadPNG_IO(SDL_IOFromMem(const_cast<unsigned char*>(icon_data), icon_data_size));
	SDL_SetWindowIcon(window, window_icon);

	SDL_ShowWindow(window);

	SDL_Texture *logo_texture{};
	ImVec2 logo_size{};
	const float logo_scale = 0.4f;
	{
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
		auto *logo = IMG_LoadPNG_IO(SDL_IOFromMem(const_cast<unsigned char *>(logo_data), logo_data_size));

		if (logo != nullptr) {
			spdlog::info("logo created");
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
	io.Fonts->AddFontFromMemoryCompressedTTF(static_cast<const void *>(font_roboto_sans_compressed_data),
											 static_cast<int>(font_roboto_sans_compressed_size), 16.0f);
	io.Fonts->AddFontFromMemoryCompressedTTF(static_cast<const void *>(font_roboto_mono_compressed_data),
											 static_cast<int>(font_roboto_mono_compressed_size), 16.0f);
	io.FontDefault = io.Fonts->Fonts[0];
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

	bool show_plot_window = true;
	std::vector<data_dict_t> data_dict{};
	const auto clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	// Main loop
	bool done{false};
	bool open_selected{false};
	bool select_folder{false};
	while (!done) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
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

				if (event.key.key == SDLK_W && (event.key.mod & SDL_KMOD_CTRL) != 0) {
					data_dict.clear();
				}

				if (event.key.key == SDLK_Q && (event.key.mod & SDL_KMOD_CTRL) != 0) {
					done = true;
				}
			}

			if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
				display_scale = SDL_GetWindowDisplayScale(window);
				io.FontGlobalScale = display_scale;
				spdlog::debug("Display scale changed to {}x", display_scale);
			}
		}

		if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0) {
			SDL_Delay(10);
			continue;
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
				if (ImGui::MenuItem("Close", "Ctrl+W")) {
					data_dict.clear();
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Exit", "Ctrl+Q", &done)) {}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Debug")) {
				if (ImGui::MenuItem("Show plot window", nullptr, &show_plot_window)) {}
				ImGui::Separator();
				if (ImGui::MenuItem("Parallel loading", nullptr, &parallel_loading)) {}
				const auto dp_str = fmt::format("Loaded data points: {}", max_loaded_data_points_current);
				ImGui::Text("%s", dp_str.c_str());	// NOLINT(hicpp-vararg)
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

			menu_size = ImGui::GetWindowSize();
			ImGui::EndMainMenuBar();
		}

		if (done) {
			stop_loading.store(true);
		}

		if (open_selected) {
			paths = selectFilesFromDialog(select_folder);

			if (!paths.empty()) {
				finished_files.store(0);

				paths_expanded = preparePaths(paths);
				required_files = paths_expanded.size();
				stop_loading.store(false);
				max_loaded_data_points.store(0);
				loading_start_time = std::chrono::steady_clock::now();

				data_dict_f =
					std::async(std::launch::async, [paths_expanded, &finished_files, &stop_loading, max_data_points,
													&max_loaded_data_points, parallel_loading] {
						return loadCSVs(paths_expanded, finished_files, stop_loading, max_data_points,
										max_loaded_data_points, parallel_loading);
					});
			}

			open_selected = false;
			select_folder = false;
		}

		if (!data_dict.empty()) {
			ImGui::SetNextWindowPos(ImVec2(0, menu_size.y));
			ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y - menu_size.y));
			ImGui::Begin("Data view", nullptr,
							ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
								ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
								ImGuiWindowFlags_NoScrollWithMouse);

			ImPlot::CreateContext();
			ImPlot::GetStyle().UseLocalTime = false;
			ImPlot::GetStyle().UseISO8601 = true;
			ImPlot::GetStyle().Use24HourClock = true;

			if (!data_dict.empty()) {
				const auto window_size = ImGui::GetWindowSize();

				ImGui::BeginChild("Column List", ImVec2(250, window_size.y - 20));
				const auto col_list_size = ImGui::GetWindowSize();
				if (ImGui::BeginListBox("Columns", ImVec2(col_list_size.x, col_list_size.y))) {
					for (auto &dct : data_dict) {
						if (ImGui::Selectable(dct.name.c_str(), &dct.visible)) {}
					}
					ImGui::EndListBox();
				}
				ImGui::EndChild();

				ImGui::SameLine();

				ImGui::BeginChild("File content", ImVec2(window_size.x - 255, window_size.y - 20));
				ImGui::PushFont(io.Fonts->Fonts[1]);
				plotDataInSubplots(data_dict);
				ImGui::PopFont();
				ImGui::EndChild();
			} else {
				ImGui::OpenPopup("Error", ImGuiWindowFlags_NoResize);
				if (ImGui::BeginPopupModal("Error")) {
					ImGui::Text("No valid data found.");  // NOLINT(hicpp-vararg)
					ImGui::EndPopup();
				}
			}

			if (!show_plot_window) {
				data_dict.clear();
			}

			ImGui::End();
		}

		if (data_dict_f.valid()) {
			ImGui::SetNextWindowSize(ImVec2(250.0f, 90.0f));
			ImGui::OpenPopup("Loading data...");
			if (ImGui::BeginPopupModal("Loading data...", nullptr,
									   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)) {
				const auto progress = static_cast<float>(finished_files.load()) / static_cast<double>(required_files);
				const auto label = fmt::format("{:.0f}% ({}/{})", progress * 100.0f, finished_files.load(),
											   static_cast<double>(required_files));
				ImGui::ProgressBar(progress, ImVec2(234.0f, 25.0f), label.c_str());
				if (ImGui::Button("Cancel", ImVec2(234.0f, 25.0f))) {
					stop_loading.store(true);
				}
				ImGui::EndPopup();
			}

			if (data_dict_f.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
				const auto temp_data_dict = data_dict_f.get();

				if (!temp_data_dict.empty()) {
					data_dict = std::move(temp_data_dict);
					data_dict.at(0).visible = true;
					max_loaded_data_points_current = max_loaded_data_points.load();
				}

				show_plot_window = true;
				loading_end_time = std::chrono::steady_clock::now();
			}
		}

		ImGui::Render();
        SDL_SetRenderDrawColorFloat(renderer, clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        SDL_RenderClear(renderer);

		const SDL_FRect texture_rect{
			.x = io.DisplaySize.x - logo_size.x - 30.0f,
			.y = io.DisplaySize.y - logo_size.y - 30.0f,
			.w = logo_size.x,
			.h = logo_size.y
		};

		SDL_RenderTexture(renderer, logo_texture, nullptr, &texture_rect); 

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