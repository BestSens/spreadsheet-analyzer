#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <future>
#include <string>
#include <vector>

#include "dicts.hpp"
#include "implot.h"
#include "spdlog/spdlog.h"
#include "string_helpers.hpp"
#include "uuid.h"
#include "uuid_generator.hpp"

class WindowContext {
public:
	using function_signature =
		std::function<std::vector<data_dict_t>(std::vector<std::filesystem::path>, size_t &, const bool &)>;

	WindowContext() = default;
	explicit WindowContext(std::vector<data_dict_t> new_data) : data{std::move(new_data)} {}

	WindowContext(const std::vector<std::filesystem::path> &paths, const function_signature& loading_fn) {
		spdlog::debug("Creating window context with UUID: {}", this->getUUID());
		this->loadFiles(paths, loading_fn);
	}

	~WindowContext() {
		spdlog::debug("Destroying window context with UUID: {}", this->getUUID());
		if (this->data_dict_f.valid()) {
			*this->stop_loading = true;
			this->data_dict_f.wait();
		}
		spdlog::debug("Window context with UUID: {} destroyed", this->getUUID());

		if (this->implot_context != nullptr) {
			ImPlot::DestroyContext(this->implot_context);
		}
	}

	WindowContext(const WindowContext &other)
		: data{other.data}, window_title{getIncrementedWindowTitle(other.window_title)} {};

	auto operator=(const WindowContext &other) -> WindowContext & {
		if (this != &other) {
			this->data = other.data;
			this->window_title = getIncrementedWindowTitle(other.window_title);
		}

		return *this;
	};

	WindowContext(WindowContext &&other) noexcept
		: data(std::move(other.data)),
		  window_open(other.window_open),
		  scheduled_for_deletion(other.scheduled_for_deletion),
		  global_x_link(other.global_x_link) {
		std::swap(this->implot_context, other.implot_context);
		std::swap(this->finished_files, other.finished_files);
		std::swap(this->stop_loading, other.stop_loading);
		std::swap(this->data_dict_f, other.data_dict_f);
		std::swap(this->required_files, other.required_files);
		std::swap(this->window_title, other.window_title);
		std::swap(this->uuid, other.uuid);
		spdlog::debug("Moved window context with UUID: {}", this->getUUID());
	}

	auto operator=(WindowContext &&other) noexcept -> WindowContext & {
		if (this != &other) {
			this->data = std::move(other.data);
			this->window_open = other.window_open;
			this->scheduled_for_deletion = other.scheduled_for_deletion;
			this->global_x_link = other.global_x_link;

			std::swap(this->implot_context, other.implot_context);
			std::swap(this->finished_files, other.finished_files);
			std::swap(this->stop_loading, other.stop_loading);
			std::swap(this->data_dict_f, other.data_dict_f);
			std::swap(this->required_files, other.required_files);
			std::swap(this->window_title, other.window_title);
			std::swap(this->uuid, other.uuid);
			spdlog::debug("Moved window context with UUID: {}", this->getUUID());
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

	auto getWindowOpenRef() -> bool & {
		return this->window_open;
	}

	auto getGlobalXLinkRef() -> bool & {
		return this->global_x_link;
	}

	auto getGlobalXLink() const -> bool {
		return this->global_x_link;
	}

	auto getForceSubplotRef() -> bool & {
		return this->force_subplot;
	}

	auto getForceSubplot() const -> bool {
		return this->force_subplot;
	}

	[[nodiscard]] auto isScheduledForDeletion() const -> bool {
		return this->scheduled_for_deletion;
	}

	auto scheduleForDeletion() -> void {
		*this->stop_loading = true;
		this->scheduled_for_deletion = true;
	}

	auto loadFiles(const std::vector<std::filesystem::path> &paths, const function_signature &fn) -> void {
		if (paths.empty()) {
			return;
		}

		if (paths.size() > 1) {
			this->window_title = paths.front().parent_path().filename().string();
		} else {
			this->window_title = paths.front().filename().string();
		}

		this->required_files = paths.size();
		this->data_dict_f = std::async(std::launch::async, [this, fn, paths]() -> std::vector<data_dict_t> {
			try {
				auto &temp_finished_files = *this->finished_files;
				const auto &temp_stop_loading = *this->stop_loading;
				return fn(paths, temp_finished_files, temp_stop_loading);
			} catch (const std::exception &e) {
				spdlog::error("error loading files for {}: {}", this->window_title, e.what());
			}

			return {};
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
			.is_loading = is_loading, .finished_files = *this->finished_files, .required_files = this->required_files};
	}

	[[nodiscard]] auto getWindowTitle() const -> std::string {
		return this->window_title;
	}

	[[nodiscard]] auto getWindowID() const -> std::string {
		return window_title + "##" + this->getUUID();
	}

	[[nodiscard]] auto getUUID() const -> std::string {
		return uuids::to_string(this->uuid);
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

	auto switchToImPlotContext() -> void {
		if (this->implot_context == nullptr) {
			this->implot_context = ImPlot::CreateContext();
			ImPlot::SetCurrentContext(this->implot_context);
			ImPlot::GetStyle().UseLocalTime = false;
			ImPlot::GetStyle().UseISO8601 = true;
			ImPlot::GetStyle().Use24HourClock = true;
			ImPlot::GetStyle().FitPadding = ImVec2(0.025f, 0.1f);
			ImPlot::GetStyle().DigitalBitHeight = 50.0f;
		} else {
			ImPlot::SetCurrentContext(this->implot_context);
		}
	}
private:
	ImPlotContext *implot_context{nullptr};
	std::vector<data_dict_t> data{};
	bool window_open{true};
	bool scheduled_for_deletion{false};
	bool global_x_link{false};
	bool force_subplot{false};
	std::future<std::vector<data_dict_t>> data_dict_f{};

	// should be fine to use these without locking as they are only written on one thread
	std::unique_ptr<bool> stop_loading{std::make_unique<bool>(false)};
	std::unique_ptr<size_t> finished_files{std::make_unique<size_t>(0)};
	size_t required_files{0};
	std::string window_title;
	uuids::uuid uuid{UUIDGenerator::getInstance().generate()};

	std::vector<std::string> assigned_plot_ids{};
};
