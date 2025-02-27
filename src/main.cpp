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

#include "SDL3/SDL_main.h"
#include "SDL3/SDL_opengl.h"
#include "SDL3_image/SDL_image.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include "csv.hpp"
#include "custom_type_traits.hpp"
#include "cxxopts.hpp"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "implot.h"
#include "nfd.hpp"
#include "spdlog/spdlog.h"
#include "utility.hpp"
#include "uuid.h"
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
		std::string uuid;
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

	class UUIDGenerator {
	public:
		// Returns the singleton instance.
		static auto getInstance() -> UUIDGenerator & {
			static UUIDGenerator instance;
			return instance;
		}

		// Delete copy constructor, move constructor, copy assignment and move assignment operators.
		UUIDGenerator(const UUIDGenerator &) = delete;
		UUIDGenerator(UUIDGenerator &&) = delete;
		auto operator=(const UUIDGenerator &) -> UUIDGenerator& = delete;
		auto operator=(UUIDGenerator &&) -> UUIDGenerator& = delete;

		// Generates a new UUID.
		auto generate() -> uuids::uuid {
			return this->generator();
		}

	private:
		// Private constructor initializes the generator.
		UUIDGenerator() = default;
		~UUIDGenerator() = default;

		std::mt19937 rng{std::random_device{}()};
		uuids::uuid_random_generator generator{rng};
	};

	class WindowContext {
	public:
		using function_signature = std::function<std::vector<data_dict_t>(
			std::vector<std::filesystem::path>, std::atomic<size_t> &, std::atomic<bool> &, bool)>;

		WindowContext() = default;
		explicit WindowContext(std::vector<data_dict_t> new_data) : data{std::move(new_data)} {}

		WindowContext(const std::vector<std::filesystem::path> &paths, function_signature loading_fn) {
			spdlog::debug("Creating window context with UUID: {}", this->getUUID());
			this->loadFiles(paths, loading_fn);
		}

		~WindowContext() {
			spdlog::debug("Destroying window context with UUID: {}", this->getUUID());
			if (this->data_dict_f.valid()) {
				this->stop_loading.store(true);
				this->data_dict_f.wait();
			}
			spdlog::debug("Window context with UUID: {} destroyed", this->getUUID());
		}

		WindowContext(const WindowContext&) = delete;
		auto operator=(const WindowContext&) -> WindowContext& = delete;

		WindowContext(WindowContext &&other) noexcept
			: data(std::move(other.data)),
			  window_open(other.window_open),
			  scheduled_for_deletion(other.scheduled_for_deletion) {}

		auto operator=(WindowContext &&other) noexcept -> WindowContext & {
			if (this != &other) {
				this->data = std::move(other.data);
				this->window_open = other.window_open;
				this->scheduled_for_deletion = other.scheduled_for_deletion;
			}

			return *this;
		}

		auto clear() -> void {
			data.clear();
		}

		[[nodiscard]] auto getData() const -> const std::vector<data_dict_t> & {
			return this->data;
		}
		
		[[nodiscard]] auto getData() -> std::vector<data_dict_t> & {
			return this->data;
		}

		auto setData(std::vector<data_dict_t> new_data) -> void {
			this->data = std::move(new_data);
		}

		auto getWindowOpenRef() -> bool& {
			return this->window_open;
		}

		[[nodiscard]] auto isScheduledForDeletion() const -> bool {
			return this->scheduled_for_deletion;
		}

		auto scheduleForDeletion() -> void {
			this->stop_loading.store(true);
			this->scheduled_for_deletion = true;
		}

		auto loadFiles(const std::vector<std::filesystem::path> &paths, function_signature fn) -> void {
			this->required_files = paths.size();
			this->data_dict_f = std::async(std::launch::async, [this, fn, paths] {
				return fn(paths, this->finished_files, this->stop_loading, false);
			});
		}

		auto checkForFinishedLoading() -> void {
			if (data_dict_f.valid() && data_dict_f.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
				const auto temp_data_dict = data_dict_f.get();

				if (!temp_data_dict.empty()) {
					this->data = temp_data_dict;
					this->data.front().visible = true;
				}
			}
		}

		struct loading_status_t {
			bool is_loading;
			size_t finished_files;
			size_t required_files;
		};

		auto getLoadingStatus() -> loading_status_t {
			const auto is_loading = this->data_dict_f.valid() &&
									this->data_dict_f.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
			return {
				.is_loading = is_loading,
				.finished_files = this->finished_files.load(),
				.required_files = this->required_files
			};
		}

		auto getWindowID() const -> std::string {
			return "Data view##" + this->getUUID();
		}

		auto getUUID() const -> std::string {
			return uuids::to_string(this->uuid);
		}

	private:
		std::vector<data_dict_t> data{};
		bool window_open{true};
		bool scheduled_for_deletion{false};
		std::future<std::vector<data_dict_t>> data_dict_f{};

		std::atomic<bool> stop_loading{false};
		std::atomic<size_t> finished_files{0};
		size_t required_files{0};
		uuids::uuid uuid{UUIDGenerator::getInstance().generate()};
	};

	auto parseDate(const std::string &str) -> time_t {
		static const auto locale = std::locale("de_DE.utf-8");
		std::istringstream ss(str);
		ss.imbue(locale);
		
		std::chrono::sys_seconds tp{};
		ss >> std::chrono::parse("%Y/%m/%d %H:%M:%S", tp);
		
		return std::chrono::system_clock::to_time_t(tp);
	}

	auto trim(std::string_view str) -> std::string_view {
		const auto pos1 = str.find_first_not_of(" \t\n\r");
		if (pos1 == std::string::npos) {
			return "";
		}

		const auto pos2 = str.find_last_not_of(" \t\n\r");
		return str.substr(pos1, pos2 - pos1 + 1);
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

		return {std::string(trim(name)), std::string(trim(unit))};
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
				  std::atomic<bool> &stop_loading, bool parallel_loading) -> std::vector<data_dict_t> {
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
			dd.uuid = uuids::to_string(UUIDGenerator::getInstance().generate());
			dd.unit = value.unit;

			for (const auto &[date, val] : value.data) {
				dd.timestamp.push_back(date);
				dd.data.push_back(val);
			}

			values.push_back(dd);
		}

		return values;
	}

	struct plot_data_t {
		const data_dict_t *data;
		size_t reduction_factor;
		size_t start_index;
		int count;
	};

	auto plotDict(int i, void *data) -> ImPlotPoint {
		assert(i >= 0);
		assert(data != nullptr);

		const auto &plot_data = *static_cast<plot_data_t *>(data);
		const auto &dd = *plot_data.data;

		if (i == 0) {
			return {static_cast<double>(dd.timestamp.front()), std::numeric_limits<double>::quiet_NaN()};
		} 

		if (i == plot_data.count - 1) {
			return {static_cast<double>(dd.timestamp.back()), std::numeric_limits<double>::quiet_NaN()};
		}

		const auto index = plot_data.start_index + (static_cast<size_t>(i-1) * plot_data.reduction_factor);
		return {static_cast<double>(dd.timestamp[index]), dd.data[index]};
	}

	auto getDateRange(const data_dict_t& data) -> std::pair<time_t, time_t> {
		if (data.timestamp.empty()) {
			return {0, 0};
		}

		return {data.timestamp.front(), data.timestamp.back()};
	}

	auto getIndicesFromTimeRange(const std::vector<time_t> date, const ImPlotRange &limits)
		-> std::pair<size_t, size_t> {
		const auto start = static_cast<time_t>(limits.Min);
		const auto stop = static_cast<time_t>(limits.Max);
		const auto start_it = std::lower_bound(date.begin(), date.end(), start);
		const auto stop_it = std::upper_bound(date.begin(), date.end(), stop);

		return {static_cast<size_t>(start_it - date.begin()), static_cast<size_t>(stop_it - date.begin())};
	}

	auto plotDataInSubplots(const std::vector<data_dict_t> &data, size_t max_data_points, const std::string &uuid)
		-> void {
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

		const auto [rows, cols] = [n_selected]() -> std::pair<int, int> {
			if (n_selected <= 3) {
				return {n_selected, 1};
			}

			return {(n_selected + 1) / 2, 2};
		}();

		const auto subplot_id = "##" + uuid;
		if (ImPlot::BeginSubplots(subplot_id.c_str(), rows, cols, ImVec2(plot_width, plot_height),
								  ImPlotSubplotFlags_ShareItems)) {
			for (const auto &col : data) {
				if (!col.visible) {
					continue;
				}

				const auto plot_title = col.name + "##" + col.uuid;
				if (ImPlot::BeginPlot(plot_title.c_str(), ImVec2(plot_width, plot_height),
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

					const auto date_range = getDateRange(col);
					ImPlot::SetupAxisLimits(ImAxis_X1, static_cast<double>(date_range.first),
											static_cast<double>(date_range.second), ImGuiCond_Once);

					const auto limits = ImPlot::GetPlotLimits(ImAxis_X1);
					const auto [start_index, stop_index] = getIndicesFromTimeRange(col.timestamp, limits.X);
					const auto points_in_range = stop_index - start_index;
					const auto reduction_factor = std::clamp(fastCeil<size_t>(points_in_range, max_data_points), 1uz,
															 std::numeric_limits<size_t>::max());

					const auto count = coerceCast<int>(fastCeil(points_in_range, reduction_factor)) + 2;
					plot_data_t plot_data{
						.data = &col, .reduction_factor = reduction_factor, .start_index = start_index, .count = count};

					ImPlot::PlotLineG(col.name.c_str(), plotDict, &plot_data, count);
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

auto main(int argc, char **argv) -> int {  // NOLINT(readability-function-cognitive-complexity)
	std::set_terminate(terminateHandler);

	std::vector<std::filesystem::path> commandline_paths{};
	int max_data_points = 10'000;
	bool show_console{false};
	
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
				spdlog::info("verbose output enabled");
				show_console = true;
			}
		} catch (const std::exception& e) {
			spdlog::critical(e.what());
			return EXIT_FAILURE;
		}

	}

	if (!show_console) {
		hideConsole();
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

	const auto background_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	// Main loop
	bool done{false};
	bool is_ctrl_pressed{false};
	bool is_shift_pressed{false};
	
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

		for (size_t i = 1; auto &ctx : window_contexts) {
			ImGui::PushID(ctx.getUUID().c_str());
			ctx.checkForFinishedLoading();
			auto &dict = ctx.getData();
			auto window_open = ctx.getWindowOpenRef();

			ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.75f, (io.DisplaySize.y - menu_size.y) * 0.75f),
									 ImGuiCond_Once);
			ImGui::SetNextWindowPos(ImVec2(static_cast<float>(i) * 25.0f, static_cast<float>(i) * 25.0f + menu_size.y),
									ImGuiCond_Once);
			++i;

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
					ImGui::PushFont(io.Fonts->Fonts[1]);
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