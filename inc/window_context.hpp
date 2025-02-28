#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <future>
#include <string>
#include <vector>

#include "spdlog/spdlog.h"
#include "uuid_generator.hpp"
#include "uuids.h"

class WindowContext {
public:
	using function_signature =
		std::function<std::vector<data_dict_t>(std::vector<std::filesystem::path>, size_t &, const bool &, bool)>;

	WindowContext() = default;
	explicit WindowContext(std::vector<data_dict_t> new_data) : data{std::move(new_data)} {}

	WindowContext(const std::vector<std::filesystem::path> &paths, function_signature loading_fn) {
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
	}

	WindowContext(const WindowContext &) = delete;
	auto operator=(const WindowContext &) -> WindowContext & = delete;

	WindowContext(WindowContext &&other) noexcept
		: data(std::move(other.data)),
		  window_open(other.window_open),
		  scheduled_for_deletion(other.scheduled_for_deletion) {}

	auto operator=(WindowContext &&other) noexcept -> WindowContext & {
		if (this != &other) {
			this->data = std::move(other.data);
			this->window_open = other.window_open;
			this->scheduled_for_deletion = other.scheduled_for_deletion;
			
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

	[[nodiscard]] auto isScheduledForDeletion() const -> bool {
		return this->scheduled_for_deletion;
	}

	auto scheduleForDeletion() -> void {
		*this->stop_loading = true;
		this->scheduled_for_deletion = true;
	}

	auto loadFiles(const std::vector<std::filesystem::path> &paths, function_signature fn) -> void {
		if (paths.empty()) {
			return;
		}

		if (paths.size() > 1) {
			this->window_title = paths.front().parent_path().filename().string();
		} else {
			this->window_title = paths.front().filename().string();
		}

		this->required_files = paths.size();
		this->data_dict_f = std::async(std::launch::async, [this, fn, paths] {
			auto &temp_finished_files = *this->finished_files;
			const auto &temp_stop_loading = *this->stop_loading;
			return fn(paths, temp_finished_files, temp_stop_loading, false);
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

	[[nodiscard]] auto getWindowID() const -> std::string {
		return window_title + "##" + this->getUUID();
	}

	[[nodiscard]] auto getUUID() const -> std::string {
		return uuids::to_string(this->uuid);
	}

private:
	std::vector<data_dict_t> data{};
	bool window_open{true};
	bool scheduled_for_deletion{false};
	std::future<std::vector<data_dict_t>> data_dict_f{};

	// should be fine to use these without locking as they are only written on one thread
	std::unique_ptr<bool> stop_loading{std::make_unique<bool>(false)};
	std::unique_ptr<size_t> finished_files{std::make_unique<size_t>(0)};
	size_t required_files{0};
	std::string window_title;
	uuids::uuid uuid{UUIDGenerator::getInstance().generate()};
};
