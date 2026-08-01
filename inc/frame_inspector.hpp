#pragma once

#include "window_context.hpp"

// Docks the inspector to the right of its stream window the first time it is shown. Has to run
// before the stream window is submitted: docking moves that window into a freshly created node.
auto placeFrameInspectorWindow(BinaryWindowContext& window_context) -> void;

// Renders the DirectView snapshot and the metadata of the frame the plot cursor is currently on
// into the inspector window belonging to the given stream.
auto renderFrameInspectorWindow(BinaryWindowContext& window_context) -> void;
