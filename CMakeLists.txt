cmake_minimum_required(VERSION 3.24)

project(spreadsheet-analyzer-2 VERSION 1.2.1)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE)
	enable_language(OBJC)
endif()

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
	src/string_helpers.cpp
	src/window_context.cpp
	src/recent_files.cpp
)

if(APPLE)
	set(MACOS_APP_ICON_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/assets/2025_02_ICON_flat.png")
	set(MACOS_APP_ICONSET_DIR "${CMAKE_CURRENT_BINARY_DIR}/app.iconset")
	set(MACOS_APP_ICON "${CMAKE_CURRENT_BINARY_DIR}/app_icon.icns")

	add_custom_command(
		OUTPUT ${MACOS_APP_ICON}
		COMMAND ${CMAKE_COMMAND} -E rm -rf ${MACOS_APP_ICONSET_DIR}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${MACOS_APP_ICONSET_DIR}
		COMMAND sips -z 16 16 ${MACOS_APP_ICON_SOURCE} --out ${MACOS_APP_ICONSET_DIR}/icon_16x16.png
		COMMAND sips -z 32 32 ${MACOS_APP_ICON_SOURCE} --out ${MACOS_APP_ICONSET_DIR}/icon_16x16@2x.png
		COMMAND sips -z 32 32 ${MACOS_APP_ICON_SOURCE} --out ${MACOS_APP_ICONSET_DIR}/icon_32x32.png
		COMMAND sips -z 64 64 ${MACOS_APP_ICON_SOURCE} --out ${MACOS_APP_ICONSET_DIR}/icon_32x32@2x.png
		COMMAND sips -z 128 128 ${MACOS_APP_ICON_SOURCE} --out ${MACOS_APP_ICONSET_DIR}/icon_128x128.png
		COMMAND sips -z 256 256 ${MACOS_APP_ICON_SOURCE} --out ${MACOS_APP_ICONSET_DIR}/icon_128x128@2x.png
		COMMAND sips -z 256 256 ${MACOS_APP_ICON_SOURCE} --out ${MACOS_APP_ICONSET_DIR}/icon_256x256.png
		COMMAND sips -z 512 512 ${MACOS_APP_ICON_SOURCE} --out ${MACOS_APP_ICONSET_DIR}/icon_256x256@2x.png
		COMMAND sips -z 512 512 ${MACOS_APP_ICON_SOURCE} --out ${MACOS_APP_ICONSET_DIR}/icon_512x512.png
		COMMAND sips -z 1024 1024 ${MACOS_APP_ICON_SOURCE} --out ${MACOS_APP_ICONSET_DIR}/icon_512x512@2x.png
		COMMAND iconutil -c icns ${MACOS_APP_ICONSET_DIR} -o ${MACOS_APP_ICON}
		COMMAND ${CMAKE_COMMAND} -E rm -rf ${MACOS_APP_ICONSET_DIR}
		DEPENDS ${MACOS_APP_ICON_SOURCE}
		VERBATIM
	)

	add_custom_target(generate_macos_icon ALL DEPENDS ${MACOS_APP_ICON})
	add_dependencies(spreadsheet_analyzer generate_macos_icon)

	target_sources(spreadsheet_analyzer PRIVATE ${MACOS_APP_ICON})
	set_source_files_properties(${MACOS_APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

	set_target_properties(spreadsheet_analyzer PROPERTIES
		MACOSX_BUNDLE TRUE
		MACOSX_BUNDLE_ICON_FILE app_icon.icns
		MACOSX_BUNDLE_BUNDLE_NAME "Spreadsheet Analyzer"
		MACOSX_BUNDLE_GUI_IDENTIFIER "com.bestsens.spreadsheet-analyzer"
		MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
		MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
	)
endif()

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
include(date)

if(APPLE)
	target_link_options(spreadsheet_analyzer PRIVATE "LINKER:-no_warn_duplicate_libraries")
endif()

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
	date

	roboto_sans
	roboto_mono
	fontawesome
)
target_include_directories(spreadsheet_analyzer PRIVATE ${OPENGL_INCLUDE_DIR})

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	find_library(GCOV_LIB gcov)
	if(GCOV_LIB)
		target_link_libraries(spreadsheet_analyzer PRIVATE ${GCOV_LIB})
	else()
		target_link_libraries(spreadsheet_analyzer PRIVATE -lgcov)
	endif()
endif()

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
		OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/logo_metax.png.h
		COMMAND xxd -n logo_data -i ${CMAKE_CURRENT_SOURCE_DIR}/assets/logo_metax.png > ${CMAKE_CURRENT_BINARY_DIR}/logo_metax.png.h
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/logo_metax.png
	)

	add_custom_target(generate_headers ALL
		DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/2025_02_ICON_shade.png.h ${CMAKE_CURRENT_BINARY_DIR}/logo_metax.png.h
	)

	add_dependencies(spreadsheet_analyzer generate_headers)
endif()
