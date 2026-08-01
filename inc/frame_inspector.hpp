#pragma once

#include "window_context.hpp"

// Renders the DirectView snapshot and the metadata of the frame the plot cursor is currently on.
auto renderFrameInspector(const BinaryWindowContext& window_context) -> void;
