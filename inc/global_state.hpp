#pragma once

#include <limits>
#include <utility>

#include "SDL3/SDL.h"

class AppState {
public:
	static auto getInstance() -> AppState& {
		static AppState instance;
		return instance;
	}

	AppState(const AppState&) = delete;
	auto operator=(const AppState&) -> AppState& = delete;
	AppState(AppState&&) = delete;
	auto operator=(AppState&&) -> AppState& = delete;
	~AppState() = default;

	// NOLINTBEGIN(misc-non-private-member-variables-in-classes)
	int max_data_points{500};

	std::pair<double, double> global_link{std::numeric_limits<double>::quiet_NaN(),
										  std::numeric_limits<double>::quiet_NaN()};
	std::pair<double, double> date_range{std::numeric_limits<double>::quiet_NaN(),
										 std::numeric_limits<double>::quiet_NaN()};
	double global_x_mouse_position{std::numeric_limits<double>::quiet_NaN()};

	bool global_x_link{false};
	bool always_show_cursor{true};
	bool always_use_subplots{false};
	
	bool is_ctrl_pressed{false};
	bool is_shift_pressed{false};

	bool show_about{false};
	bool show_debug_menu{false};
	
	float display_scale{1.0f};
	SDL_Renderer* renderer{nullptr};
	SDL_Surface* window_icon{nullptr};
	// NOLINTEND(misc-non-private-member-variables-in-classes)

private:
	AppState() = default;
};
