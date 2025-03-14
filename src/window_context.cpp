#include "window_context.hpp"

#include <ranges>
#include <string>
#include <string_view>
#include <variant>

#include "global_state.hpp"
#include "spdlog/spdlog.h"

auto getUniqueWindowTitle(const std::string_view title) -> std::string {
	const auto &ctxs = AppState::getInstance().window_contexts;

	const auto stripped_title = [&title]() -> std::string_view {
		const auto pos = title.find_last_of('(');
		if (pos == std::string::npos) {
			return title;
		}

		return title.substr(0, pos - 1);
	}();

	auto is_unique = [&]() {
		return std::ranges::none_of(ctxs, [&](const auto &ctx) {
			return std::visit([&](const auto &ctx) { return ctx.getWindowTitle() == stripped_title; }, ctx);
		});
	}();

	std::string new_title{stripped_title};

	while (!is_unique) {
		new_title = getIncrementedWindowTitle(new_title);
		is_unique = std::ranges::none_of(ctxs, [&](const auto &ctx) {
			return std::visit([&](const auto &ctx) { return ctx.getWindowTitle() == new_title; }, ctx);
		});
	}

	return new_title;
}
