#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <future>
#include <string>
#include <variant>
#include <vector>

#include "dicts.hpp"
#include "implot.h"
#include "raw_handling.hpp"
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
			ImPlot::GetStyle().UseLocalTime = false;
			ImPlot::GetStyle().UseISO8601 = true;
			ImPlot::GetStyle().Use24HourClock = true;
			ImPlot::GetStyle().FitPadding = ImVec2(0.025f, 0.1f);
			ImPlot::GetStyle().DigitalBitHeight = 50.0f;
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

class CSVWindowContext : public WindowContext {
public:
	using function_signature =
		std::function<std::vector<data_dict_t>(std::vector<std::filesystem::path>, size_t &, const bool &)>;

	CSVWindowContext() = default;
	explicit CSVWindowContext(std::vector<data_dict_t> new_data) : data{std::move(new_data)} {}

	CSVWindowContext(const std::vector<std::filesystem::path> &paths, const function_signature& loading_fn) {
		spdlog::debug("Creating csv window context with UUID: {}", this->getUUID());
		this->loadFiles(paths, loading_fn);
	}

	~CSVWindowContext() override {
		spdlog::debug("Destroying csv window context with UUID: {}", this->getUUID());
		if (this->data_dict_f.valid()) {
			*this->stop_loading = true;
			this->data_dict_f.wait();
		}
		spdlog::debug("Window csv context with UUID: {} destroyed", this->getUUID());
	}

	CSVWindowContext(const CSVWindowContext &other) : WindowContext(std::move(other)), data{other.data} {};

	auto operator=(const CSVWindowContext &other) -> CSVWindowContext & {
		if (this != &other) {
			this->data = other.data;
		}

		return *this;
	};

	CSVWindowContext(CSVWindowContext &&other) noexcept
		: WindowContext(std::move(other)), data{std::move(other.data)}, global_x_link{other.global_x_link} {
		std::swap(this->finished_files, other.finished_files);
		std::swap(this->stop_loading, other.stop_loading);
		std::swap(this->data_dict_f, other.data_dict_f);
		std::swap(this->required_files, other.required_files);
		spdlog::debug("Moved window context with UUID: {}", this->getUUID());
	}

	auto operator=(CSVWindowContext &&other) noexcept -> CSVWindowContext & {
		if (this != &other) {
			this->data = std::move(other.data);
			this->global_x_link = other.global_x_link;

			std::swap(this->finished_files, other.finished_files);
			std::swap(this->stop_loading, other.stop_loading);
			std::swap(this->data_dict_f, other.data_dict_f);
			std::swap(this->required_files, other.required_files);
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

	auto scheduleForDeletion() -> void {
		*this->stop_loading = true;
		WindowContext::scheduleForDeletion();
	}

	auto loadFiles(const std::vector<std::filesystem::path> &paths, const function_signature &fn) -> void {
		if (paths.empty()) {
			return;
		}

		const auto temp_title = [&paths]() -> std::string {
			if (paths.size() > 1) {
				return paths.front().parent_path().filename().string();
			}
			return paths.front().filename().string();
		}();

		this->setWindowTitle(getUniqueWindowTitle(temp_title));
		this->required_files = paths.size();

		// NOLINTNEXTLINE(bugprone-exception-escape)
		this->data_dict_f =
			std::async(std::launch::async, [this, fn, paths, &temp_title]() -> std::vector<data_dict_t> {
				try {
					auto &temp_finished_files = *this->finished_files;
					const auto &temp_stop_loading = *this->stop_loading;
					return fn(paths, temp_finished_files, temp_stop_loading);
				} catch (const std::exception &e) {
					spdlog::error("error loading files for {}: {}", temp_title, e.what());
				} catch (...) {
					spdlog::error("error loading files for {}", temp_title);
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

	[[nodiscard]] auto getAssignedPlotIDs() const -> std::vector<std::string> {
		return this->assigned_plot_ids;
	}

	[[nodiscard]] auto getAssignedPlotIDsRef() -> std::vector<std::string> & {
		return this->assigned_plot_ids;
	}

	auto setAssignedPlotIDs(const std::vector<std::string> &ids) -> void {
		this->assigned_plot_ids = ids;
	}

private:
	std::vector<data_dict_t> data{};
	bool global_x_link{false};
	bool force_subplot{false};
	std::future<std::vector<data_dict_t>> data_dict_f{};

	// should be fine to use these without locking as they are only written on one thread
	std::unique_ptr<bool> stop_loading{std::make_unique<bool>(false)};
	std::unique_ptr<size_t> finished_files{std::make_unique<size_t>(0)};
	size_t required_files{0};

	std::vector<std::string> assigned_plot_ids{};
};

class RawWindowContext : public WindowContext {
public:
	RawWindowContext() = default;
	explicit RawWindowContext(std::vector<downsampled_data_t> new_data) : data{std::move(new_data)} {}

	[[nodiscard]] auto getData() const -> const std::vector<downsampled_data_t> & {
		return this->data;
	}

	[[nodiscard]] auto getData() -> std::vector<downsampled_data_t> & {
		return this->data;
	}

	auto setData(std::vector<downsampled_data_t> new_data) -> void {
		this->data = std::move(new_data);
	}
private:
	std::vector<downsampled_data_t> data{};
};