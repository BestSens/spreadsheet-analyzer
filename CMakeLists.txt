cmake_minimum_required(VERSION 3.24)

project(spreadsheet-analyzer-2 VERSION 2.0.0)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cpm")

option(USE_LTO "Enable LTO" ON)

if(USE_LTO AND NOT (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeDbg"))
	include(CheckIPOSupported)
	check_ipo_supported(RESULT supported OUTPUT error)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

add_executable(spreadsheet_analyzer
	src/main.cpp
	src/window_icon.c
	FiraCode-Regular.c
	assets/manifest.manifest
	assets/app.rc
)

target_include_directories(spreadsheet_analyzer PRIVATE
	inc
)

target_compile_options(spreadsheet_analyzer PRIVATE
	-Wall -Wextra -Wpedantic -Wtype-limits
	"$<$<CONFIG:RELEASE>:-O3;-DNDEBUG>"
	"$<$<CONFIG:DEBUG>:-Og;-DDEBUG;-funwind-tables;-fno-inline>"
)

find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)

include(imgui)
include(sdl)
include(sdl-image)
include(fmt)
include(spdlog)
include(implot)
include(csv-parser)
include(add_cxxopts)
include(nativefiledialog-extended)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_BUILD_TYPE MATCHES "Debug")
	target_compile_options(spreadsheet_analyzer PRIVATE
		-fprofile-instr-generate
		-fcoverage-mapping
	)
	target_link_options(spreadsheet_analyzer PRIVATE
		-fprofile-instr-generate
		-fcoverage-mapping
	)
endif()

target_link_libraries(spreadsheet_analyzer PRIVATE
	imgui
	implot
	SDL3::SDL3
	SDL3_image-static
	Threads::Threads
	${OPENGL_LIBRARIES}
	fmt
	spdlog
	csv
	cxxopts
	nfd
)
target_include_directories(spreadsheet_analyzer PRIVATE ${OPENGL_INCLUDE_DIR})

add_custom_command(
	OUTPUT FiraCode-Regular.c
	COMMAND ${CMAKE_CURRENT_BINARY_DIR}/binary_to_compressed.exe
		-nostatic
		${CMAKE_CURRENT_SOURCE_DIR}/assets/FiraCode-Regular.ttf
		font_fira_code
		> FiraCode-Regular.c
	DEPENDS binary_to_compressed
)