#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "binary_handling.hpp"
#include "dicts.hpp"
#include "implot.h"
#include "spdlog/spdlog.h"
#include "string_helpers.hpp"
#include "uuid.h"
#include "uuid_generator.hpp"

[[nodiscard]] auto getUniqueWindowTitle(std::string_view title) -> std::string;

class WindowContext {
public:
	WindowContext() = default;
	explicit WindowContext(std::string title) : window_title{std::move(title)} {
		spdlog::debug("Creating window context with UUID: {}", this->getUUID());
	}

	virtual ~WindowContext() {
		spdlog::debug("Destroying window context with UUID: {}", this->getUUID());
		if (this->implot_context != nullptr) {
			ImPlot::DestroyContext(this->implot_context);
		}
		spdlog::debug("Window context with UUID: {} destroyed", this->getUUID());
	};

	WindowContext(const WindowContext &other) : window_title{getUniqueWindowTitle(other.window_title)} {}

	auto operator=(const WindowContext &other) -> WindowContext & {
		if (this != &other) {
			this->window_title = getUniqueWindowTitle(other.window_title);
		}

		return *this;
	};

	WindowContext(WindowContext &&other) noexcept
		: window_open(other.window_open), scheduled_for_deletion(other.scheduled_for_deletion) {
		std::swap(this->implot_context, other.implot_context);
		std::swap(this->window_title, other.window_title);
		std::swap(this->uuid, other.uuid);
		spdlog::debug("Moved window context with UUID: {}", this->getUUID());
	}

	auto operator=(WindowContext &&other) noexcept -> WindowContext & {
		if (this != &other) {
			this->window_open = other.window_open;
			this->scheduled_for_deletion = other.scheduled_for_deletion;
			std::swap(this->implot_context, other.implot_context);
			std::swap(this->window_title, other.window_title);
			std::swap(this->uuid, other.uuid);
		}

		return *this;
	}

	auto getWindowOpenRef() -> bool & {
		return this->window_open;
	}

	[[nodiscard]] auto getWindowTitle() const -> std::string {
		return this->window_title;
	}

	[[nodiscard]] auto getWindowID() const -> std::string {
		return this->window_title + "##" + this->getUUID();
	}

	[[nodiscard]] auto getUUID() const -> std::string {
		return uuids::to_string(this->uuid);
	}

	[[nodiscard]] auto isScheduledForDeletion() const -> bool {
		return this->scheduled_for_deletion;
	}

	auto scheduleForDeletion() -> void {
		this->scheduled_for_deletion = true;
	}

	auto switchToImPlotContext() -> void {
		if (this->implot_context == nullptr) {
			this->implot_context = ImPlot::CreateContext();
			ImPlot::SetCurrentContext(this->implot_context);
			ImPlot::GetStyle().UseLocalTime = true;
			ImPlot::GetStyle().UseISO8601 = true;
			ImPlot::GetStyle().Use24HourClock = true;
			ImPlot::GetStyle().FitPadding = ImVec2(0.025f, 0.1f);
		} else {
			ImPlot::SetCurrentContext(this->implot_context);
		}
	}

protected:
	auto setWindowTitle(std::string title) -> void {
		this->window_title = std::move(title);
	}

private:
	ImPlotContext *implot_context{nullptr};
	bool window_open{true};
	bool scheduled_for_deletion{false};
	std::string window_title;
	uuids::uuid uuid{UUIDGenerator::getInstance().generate()};
};

// Everything the UI needs from a loaded dataset, independent of the file format it came from.
class DataWindowContext : public WindowContext {
public:
	using WindowContext::WindowContext;

	DataWindowContext() = default;
	~DataWindowContext() override = default;

	DataWindowContext(const DataWindowContext &other)
		: WindowContext(other), data{other.data}, global_x_link{other.global_x_link},
		  last_local_x_range{other.last_local_x_range}, force_subplot{other.force_subplot},
		  force_single_plot{other.force_single_plot} {}

	auto operator=(const DataWindowContext &other) -> DataWindowContext & {
		if (this != &other) {
			WindowContext::operator=(other);
			this->data = other.data;
			this->global_x_link = other.global_x_link;
			this->last_local_x_range = other.last_local_x_range;
			this->force_subplot = other.force_subplot;
			this->force_single_plot = other.force_single_plot;
		}

		return *this;
	}

	DataWindowContext(DataWindowContext &&other) noexcept
		: WindowContext(std::move(other)), data{std::move(other.data)}, global_x_link{other.global_x_link},
		  last_local_x_range{other.last_local_x_range}, force_subplot{other.force_subplot},
		  force_single_plot{other.force_single_plot}, assigned_plot_ids{std::move(other.assigned_plot_ids)} {
		std::swap(this->stop_loading, other.stop_loading);
		std::swap(this->finished_files, other.finished_files);
		std::swap(this->current_file_progress, other.current_file_progress);
		std::swap(this->required_files, other.required_files);
	}

	auto operator=(DataWindowContext &&other) noexcept -> DataWindowContext & {
		if (this != &other) {
			WindowContext::operator=(std::move(other));
			this->data = std::move(other.data);
			this->global_x_link = other.global_x_link;
			this->last_local_x_range = other.last_local_x_range;
			this->force_subplot = other.force_subplot;
			this->force_single_plot = other.force_single_plot;
			this->assigned_plot_ids = std::move(other.assigned_plot_ids);

			std::swap(this->stop_loading, other.stop_loading);
			std::swap(this->finished_files, other.finished_files);
			std::swap(this->current_file_progress, other.current_file_progress);
			std::swap(this->required_files, other.required_files);
		}

		return *this;
	}

	auto clear() -> void {
		this->data.clear();
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

	auto getGlobalXLinkRef() -> bool & {
		return this->global_x_link;
	}

	[[nodiscard]] auto getGlobalXLink() const -> bool {
		return this->global_x_link;
	}

	auto getForceSubplotRef() -> bool & {
		return this->force_subplot;
	}

	[[nodiscard]] auto getForceSubplot() const -> bool {
		return this->force_subplot;
	}

	auto getForceSinglePlotRef() -> bool & {
		return this->force_single_plot;
	}

	[[nodiscard]] auto getForceSinglePlot() const -> bool {
		return this->force_single_plot;
	}

	auto setLastLocalXRange(const std::pair<double, double> &range) -> void {
		this->last_local_x_range = range;
	}

	[[nodiscard]] auto getLastLocalXRange() const -> std::pair<double, double> {
		return this->last_local_x_range;
	}

	[[nodiscard]] auto getAssignedPlotIDs() const -> std::vector<std::string> {
		return this->assigned_plot_ids;
	}

	[[nodiscard]] auto getAssignedPlotIDsRef() -> std::vector<std::string> & {
		return this->assigned_plot_ids;
	}

	auto setAssignedPlotIDs(const std::vector<std::string> &ids) -> void {
		this->assigned_plot_ids = ids;
	}

	auto scheduleForDeletion() -> void {
		*this->stop_loading = true;
		WindowContext::scheduleForDeletion();
	}

	struct loading_status_t {
		bool is_loading;
		size_t finished_files;
		size_t required_files;
		double current_file_progress;
	};

	[[nodiscard]] auto getLoadingStatus() const -> loading_status_t {
		return {.is_loading = this->isLoading(),
				.finished_files = *this->finished_files,
				.required_files = this->required_files,
				.current_file_progress = *this->current_file_progress};
	}

	// Polled once per frame; picks up the result of the background load when it is ready.
	virtual auto checkForFinishedLoading() -> void {}

	// Message shown instead of the plot when the load produced nothing usable.
	[[nodiscard]] virtual auto getLoadErrorMessage() const -> std::string_view {
		return {};
	}

protected:
	[[nodiscard]] virtual auto isLoading() const -> bool {
		return false;
	}

	[[nodiscard]] auto getStopLoadingFlag() -> std::atomic<bool> & {
		return *this->stop_loading;
	}

	[[nodiscard]] auto getFinishedFilesRef() -> size_t & {
		return *this->finished_files;
	}

	[[nodiscard]] auto getCurrentFileProgressRef() -> double & {
		return *this->current_file_progress;
	}

	// Resets the per-load progress so retries do not accumulate old progress.
	auto resetLoadingProgress(size_t required) -> void {
		*this->stop_loading = false;
		*this->finished_files = 0;
		*this->current_file_progress = 0.0;
		this->required_files = required;
	}

	// Derives the window title from the selection: a single file keeps its name, several files
	// are named after the folder holding them.
	auto setTitleFromPaths(const std::vector<std::filesystem::path> &paths) -> void {
		if (paths.empty() || !this->getWindowTitle().empty()) {
			return;
		}

		const auto title = paths.size() > 1 ? paths.front().parent_path().filename().string()
										    : paths.front().filename().string();
		this->setWindowTitle(getUniqueWindowTitle(title));
	}

private:
	std::vector<data_dict_t> data{};
	bool global_x_link{false};
	std::pair<double, double> last_local_x_range{std::numeric_limits<double>::quiet_NaN(),
												 std::numeric_limits<double>::quiet_NaN()};
	bool force_subplot{false};
	bool force_single_plot{false};

	std::vector<std::string> assigned_plot_ids{};

	// should be fine to use these without locking as they are only written on one thread
	std::unique_ptr<std::atomic<bool>> stop_loading{std::make_unique<std::atomic<bool>>(false)};
	std::unique_ptr<size_t> finished_files{std::make_unique<size_t>(0)};
	std::unique_ptr<double> current_file_progress{std::make_unique<double>(0.0)};
	size_t required_files{0};
};

class CSVWindowContext : public DataWindowContext {
public:
	using function_signature =
		std::function<std::vector<data_dict_t>(std::vector<std::filesystem::path>, size_t &, const std::atomic<bool> &,
											   const csv_parse_config_t &, std::string &, double &)>;

	CSVWindowContext() = default;
	explicit CSVWindowContext(std::vector<data_dict_t> new_data) {
		this->setData(std::move(new_data));
	}

	CSVWindowContext(const std::vector<std::filesystem::path> &paths, const function_signature& loading_fn,
	                 bool force_config_dialog = false) {
		spdlog::debug("Creating csv window context with UUID: {}", this->getUUID());
		this->loadFiles(paths, loading_fn, force_config_dialog);
	}

	~CSVWindowContext() override {
		spdlog::debug("Destroying csv window context with UUID: {}", this->getUUID());
		if (this->data_dict_f.valid()) {
			this->getStopLoadingFlag() = true;
			this->data_dict_f.wait();
		}
		spdlog::debug("Window csv context with UUID: {} destroyed", this->getUUID());
	}

	CSVWindowContext(const CSVWindowContext &other) : DataWindowContext(other) {};

	auto operator=(const CSVWindowContext &other) -> CSVWindowContext & {
		if (this != &other) {
			DataWindowContext::operator=(other);
		}

		return *this;
	};

	CSVWindowContext(CSVWindowContext &&other) noexcept
		: DataWindowContext(std::move(other)), stored_paths{std::move(other.stored_paths)},
		  stored_fn{std::move(other.stored_fn)}, current_config{other.current_config},
		  needs_config_dialog{other.needs_config_dialog}, config_popup_opened{other.config_popup_opened},
		  suggest_config_in_dialog{other.suggest_config_in_dialog} {
		std::swap(this->data_dict_f, other.data_dict_f);
		std::swap(this->parse_error_sample, other.parse_error_sample);
		spdlog::debug("Moved window context with UUID: {}", this->getUUID());
	}

	auto operator=(CSVWindowContext &&other) noexcept -> CSVWindowContext & {
		if (this != &other) {
			DataWindowContext::operator=(std::move(other));
			this->stored_paths  = std::move(other.stored_paths);
			this->stored_fn     = std::move(other.stored_fn);
			this->current_config       = other.current_config;
			this->needs_config_dialog  = other.needs_config_dialog;
			this->config_popup_opened  = other.config_popup_opened;
			this->suggest_config_in_dialog = other.suggest_config_in_dialog;

			std::swap(this->data_dict_f, other.data_dict_f);
			std::swap(this->parse_error_sample, other.parse_error_sample);
		}

		return *this;
	}

	auto loadFiles(const std::vector<std::filesystem::path> &paths, const function_signature &fn,
	               bool force_config_dialog = false) -> void {
		if (paths.empty()) {
			return;
		}

		this->resetLoadingProgress(paths.size());

		this->stored_paths = paths;
		this->stored_fn    = fn;
		this->needs_config_dialog  = false;
		this->config_popup_opened  = false;
		this->suggest_config_in_dialog = force_config_dialog;
		this->parse_error_sample->clear();

		this->setTitleFromPaths(paths);

		if (force_config_dialog) {
			this->needs_config_dialog = true;
			return;
		}

		// NOLINTNEXTLINE(bugprone-exception-escape)
		this->data_dict_f = std::async(
			std::launch::async,
			[this, fn, paths, config = this->current_config,
			 title = this->getWindowTitle()]() -> std::vector<data_dict_t> {
				try {
					return fn(paths, this->getFinishedFilesRef(), this->getStopLoadingFlag(), config,
					          *this->parse_error_sample, this->getCurrentFileProgressRef());
				} catch (const std::exception &e) {
					spdlog::error("error loading files for {}: {}", title, e.what());
				} catch (...) {
					spdlog::error("error loading files for {}", title);
				}

				return {};
			});
	}

	auto checkForFinishedLoading() -> void override {
		if (data_dict_f.valid() && data_dict_f.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
			const auto temp_data_dict = data_dict_f.get();

			if (!temp_data_dict.empty()) {
				this->setData(temp_data_dict);
				this->getData().front().visible = true;
				this->needs_config_dialog = false;
			} else if (!this->parse_error_sample->empty()) {
				this->needs_config_dialog = true;
			}
		}
	}

	[[nodiscard]] auto needsConfigDialog() const -> bool { return this->needs_config_dialog; }
	[[nodiscard]] auto isConfigPopupOpened() const -> bool { return this->config_popup_opened; }
	[[nodiscard]] auto shouldSuggestConfigInDialog() const -> bool { return this->suggest_config_in_dialog; }
	auto markPopupOpened() -> void { this->config_popup_opened = true; }
	[[nodiscard]] auto getParseErrorSample() const -> std::string_view { return *this->parse_error_sample; }
	[[nodiscard]] auto getStoredPaths() const -> const std::vector<std::filesystem::path> & { return this->stored_paths; }
	[[nodiscard]] auto getCurrentConfig() const -> const csv_parse_config_t & { return this->current_config; }
	auto applySuggestedConfig(csv_parse_config_t config) -> void { this->current_config = std::move(config); }

	auto retryWithConfig(csv_parse_config_t config) -> void {
		this->current_config      = std::move(config);
		this->needs_config_dialog = false;
		this->config_popup_opened = false;
		this->suggest_config_in_dialog = false;
		this->loadFiles(this->stored_paths, this->stored_fn);
	}

	auto cancelConfigDialog() -> void {
		this->needs_config_dialog = false;
		this->config_popup_opened = false;
		this->scheduleForDeletion();
	}

protected:
	[[nodiscard]] auto isLoading() const -> bool override {
		return this->data_dict_f.valid() &&
			   this->data_dict_f.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
	}

private:
	std::future<std::vector<data_dict_t>> data_dict_f{};

	// CSV import config dialog state
	std::vector<std::filesystem::path> stored_paths{};
	function_signature stored_fn{};
	csv_parse_config_t current_config{};
	std::shared_ptr<std::string> parse_error_sample{std::make_shared<std::string>()};
	bool needs_config_dialog{false};
	bool config_popup_opened{false};
	bool suggest_config_in_dialog{false};
};

// A window backed by BeMoS one raw data files. Next to the plottable columns it keeps the frames
// themselves around so the DirectView snapshot and the frame metadata can be inspected.
class BinaryWindowContext : public DataWindowContext {
public:
	using function_signature = std::function<binary_data_t(std::vector<std::filesystem::path>, size_t &,
														   const std::atomic<bool> &, std::string &, double &)>;

	BinaryWindowContext() = default;

	BinaryWindowContext(const std::vector<std::filesystem::path> &paths, const function_signature &loading_fn) {
		spdlog::debug("Creating binary window context with UUID: {}", this->getUUID());
		this->loadFiles(paths, loading_fn);
	}

	~BinaryWindowContext() override {
		spdlog::debug("Destroying binary window context with UUID: {}", this->getUUID());
		if (this->binary_data_f.valid()) {
			this->getStopLoadingFlag() = true;
			this->binary_data_f.wait();
		}
		spdlog::debug("Binary window context with UUID: {} destroyed", this->getUUID());
	}

	BinaryWindowContext(const BinaryWindowContext &other)
		: DataWindowContext(other), binary_data{other.binary_data}, show_inspector{other.show_inspector} {};

	auto operator=(const BinaryWindowContext &other) -> BinaryWindowContext & {
		if (this != &other) {
			DataWindowContext::operator=(other);
			this->binary_data = other.binary_data;
			this->show_inspector = other.show_inspector;
		}

		return *this;
	};

	BinaryWindowContext(BinaryWindowContext &&other) noexcept
		: DataWindowContext(std::move(other)), binary_data{std::move(other.binary_data)},
		  show_inspector{other.show_inspector} {
		std::swap(this->binary_data_f, other.binary_data_f);
		std::swap(this->load_error, other.load_error);
		spdlog::debug("Moved binary window context with UUID: {}", this->getUUID());
	}

	auto operator=(BinaryWindowContext &&other) noexcept -> BinaryWindowContext & {
		if (this != &other) {
			DataWindowContext::operator=(std::move(other));
			this->binary_data = std::move(other.binary_data);
			this->show_inspector = other.show_inspector;

			std::swap(this->binary_data_f, other.binary_data_f);
			std::swap(this->load_error, other.load_error);
		}

		return *this;
	}

	auto loadFiles(const std::vector<std::filesystem::path> &paths, const function_signature &fn) -> void {
		if (paths.empty()) {
			return;
		}

		this->resetLoadingProgress(paths.size());
		this->load_error->clear();
		this->setTitleFromPaths(paths);

		// NOLINTNEXTLINE(bugprone-exception-escape)
		this->binary_data_f = std::async(
			std::launch::async, [this, fn, paths, title = this->getWindowTitle()]() -> binary_data_t {
				try {
					return fn(paths, this->getFinishedFilesRef(), this->getStopLoadingFlag(), *this->load_error,
							  this->getCurrentFileProgressRef());
				} catch (const std::exception &e) {
					spdlog::error("error loading files for {}: {}", title, e.what());
				} catch (...) {
					spdlog::error("error loading files for {}", title);
				}

				return {};
			});
	}

	auto checkForFinishedLoading() -> void override {
		if (this->binary_data_f.valid() &&
			this->binary_data_f.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
			this->binary_data = this->binary_data_f.get();

			if (!this->binary_data.columns.empty()) {
				this->setData(this->binary_data.columns);
				this->getData().front().visible = true;
			}
		}
	}

	[[nodiscard]] auto getLoadErrorMessage() const -> std::string_view override {
		return *this->load_error;
	}

	[[nodiscard]] auto getBinaryData() const -> const binary_data_t & {
		return this->binary_data;
	}

	auto getShowInspectorRef() -> bool & {
		return this->show_inspector;
	}

	// Index of the frame covering the given time, clamped to the available range.
	[[nodiscard]] auto getFrameIndexForTime(double time) const -> size_t {
		const auto &frames = this->binary_data.frames;

		if (frames.empty()) {
			return 0;
		}

		if (!std::isfinite(time)) {
			return 0;
		}

		const auto it = std::ranges::upper_bound(frames, time, std::ranges::less{}, &binary_frame_t::t0);

		if (it == frames.begin()) {
			return 0;
		}

		return static_cast<size_t>(std::distance(frames.begin(), it) - 1);
	}

protected:
	[[nodiscard]] auto isLoading() const -> bool override {
		return this->binary_data_f.valid() &&
			   this->binary_data_f.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
	}

private:
	binary_data_t binary_data{};
	std::future<binary_data_t> binary_data_f{};
	std::shared_ptr<std::string> load_error{std::make_shared<std::string>()};
	bool show_inspector{true};
};
