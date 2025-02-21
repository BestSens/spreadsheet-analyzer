include(CPM)
CPMAddPackage(
  NAME imgui_external
  VERSION 1.91.8
  URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.8-docking.zip
  URL_HASH MD5=43d165df3b3cbea8717873ba213d0c28
  OVERRIDE_FIND_PACKAGE
)

FetchContent_MakeAvailable(imgui_external)

include(sdl)

add_library(imgui
	${imgui_external_SOURCE_DIR}/imgui.cpp
	${imgui_external_SOURCE_DIR}/imgui_draw.cpp
	${imgui_external_SOURCE_DIR}/imgui_tables.cpp
	${imgui_external_SOURCE_DIR}/imgui_widgets.cpp
	${imgui_external_SOURCE_DIR}/imgui_demo.cpp
	${imgui_external_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
	${imgui_external_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${imgui_external_SOURCE_DIR} ${imgui_external_SOURCE_DIR}/misc/cpp)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

add_executable(binary_to_compressed
	${imgui_external_SOURCE_DIR}/misc/fonts/binary_to_compressed_c.cpp
)