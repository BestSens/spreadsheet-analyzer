cmake_minimum_required(VERSION 3.24)

project(spreadsheet-analyzer-2 VERSION 1.0.1)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cpm")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(USE_LTO "Enable LTO" ON)

set(BUILD_STATIC ON)

if(USE_LTO AND NOT (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeDbg"))
	include(CheckIPOSupported)
	check_ipo_supported(RESULT supported OUTPUT error)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

configure_file(assets/app.rc.in app.rc @ONLY)

add_executable(spreadsheet_analyzer
	src/main.cpp
	src/about_screen.cpp
	src/imgui_extensions.cpp
	src/csv_handling.cpp
	src/plotting.cpp
	src/file_dialog.cpp
	src/winapi.cpp
	src/fonts.cpp
	src/textures.c
)

if(WIN32)
	target_sources(spreadsheet_analyzer PRIVATE
		assets/manifest.manifest
		${CMAKE_CURRENT_BINARY_DIR}/app.rc
	)
endif()

target_include_directories(spreadsheet_analyzer PRIVATE
	inc
)

target_compile_options(spreadsheet_analyzer PRIVATE
	"$<$<CONFIG:RELEASE>:-O3;-DNDEBUG>"
	"$<$<CONFIG:DEBUG>:-Og;-DDEBUG;-funwind-tables;-fno-inline;-fno-omit-frame-pointer>"
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
include(expected)
include(uuid)
include(fontawesome)
include(roboto_sans)
include(roboto_mono)
include(compiler_warnings)
include(version)
include(fast_float)

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
	common_warnings
	bestsens_version

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
	expected
	stduuid
	fast_float

	roboto_sans
	roboto_mono
	fontawesome
)
target_include_directories(spreadsheet_analyzer PRIVATE ${OPENGL_INCLUDE_DIR})

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)
	target_include_directories(spreadsheet_analyzer PRIVATE
		${CMAKE_CURRENT_BINARY_DIR}
	)

	add_custom_command(
		OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/2025_02_ICON_shade.png.h
		COMMAND xxd -n icon_data -i ${CMAKE_CURRENT_SOURCE_DIR}/assets/2025_02_ICON_shade.png > ${CMAKE_CURRENT_BINARY_DIR}/2025_02_ICON_shade.png.h
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/2025_02_ICON_shade.png
	)

	add_custom_command(
		OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/logo_4c.png.h
		COMMAND xxd -n logo_data -i ${CMAKE_CURRENT_SOURCE_DIR}/assets/logo_4c.png > ${CMAKE_CURRENT_BINARY_DIR}/logo_4c.png.h
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/logo_4c.png
	)

	add_custom_target(generate_headers ALL
		DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/2025_02_ICON_shade.png.h ${CMAKE_CURRENT_BINARY_DIR}/logo_4c.png.h
	)

	add_dependencies(spreadsheet_analyzer generate_headers)
endif()
