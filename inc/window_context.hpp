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
		this->stop_loading.store(true);
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
		return {.is_loading = is_loading,
				.finished_files = this->finished_files.load(),
				.required_files = this->required_files};
	}

	auto getWindowID() const -> std::string {
		return window_title + "##" + this->getUUID();
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
	std::string window_title;
	uuids::uuid uuid{UUIDGenerator::getInstance().generate()};
};
