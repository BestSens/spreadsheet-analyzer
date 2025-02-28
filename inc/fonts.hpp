#pragma once

#include <cstdint>

#include "imgui.h"


enum class fontList : std::uint8_t {
	ROBOTO_SANS_16,
	ROBOTO_MONO_16,
	ROBOTO_MONO_20
};

auto addFonts() -> void;
auto getFont(fontList font) -> ImFont *;
