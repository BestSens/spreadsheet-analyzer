cmake_minimum_required(VERSION 3.24)

project(spreadsheet-analyzer-2 VERSION 2.0.0)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cpm")

option(USE_LTO "Enable LTO" ON)

set(BUILD_STATIC ON)

if(USE_LTO AND NOT (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeDbg"))
	include(CheckIPOSupported)
	check_ipo_supported(RESULT supported OUTPUT error)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

add_executable(spreadsheet_analyzer
	src/main.cpp
	src/winapi.cpp
	src/textures.c
)

if(WIN32)
	target_sources(spreadsheet_analyzer PRIVATE
		assets/manifest.manifest
		assets/app.rc
	)
endif()

target_include_directories(spreadsheet_analyzer PRIVATE
	inc
)

set(CLANG_WARNINGS
	-Wall
	-Wextra # reasonable and standard
	-Wshadow # warn the user if a variable declaration shadows one from a parent context
	-Wnon-virtual-dtor # warn the user if a class with virtual functions has a non-virtual destructor. This helps
					# catch hard to track down memory errors
	-Wold-style-cast # warn for c-style casts
	-Wcast-align # warn for potential performance problem casts
	-Wunused # warn on anything being unused
	-Woverloaded-virtual # warn if you overload (not override) a virtual function
	-Wpedantic # warn if non-standard C++ is used
	-Wconversion # warn on type conversions that may lose data
	-Wsign-conversion # warn on sign conversions
	-Wnull-dereference # warn if a null dereference is detected
	-Wdouble-promotion # warn if float is implicit promoted to double
	-Wformat=2 # warn on security issues around functions that format output (ie printf)
)

set(GCC_WARNINGS
	${CLANG_WARNINGS}
	-Wmisleading-indentation # warn if indentation implies blocks where blocks do not exist
	-Wduplicated-cond # warn if if / else chain has duplicated conditions
	-Wduplicated-branches # warn if if / else branches have duplicated code
	-Wlogical-op # warn about logical operations being used where bitwise were probably wanted
	-Wuseless-cast # warn if you perform a cast to the same type
)

if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
	set(PROJECT_WARNINGS ${CLANG_WARNINGS})
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	set(PROJECT_WARNINGS ${GCC_WARNINGS})
else()
	message(AUTHOR_WARNING "No compiler warnings set for '${CMAKE_CXX_COMPILER_ID}' compiler.")
endif()

target_compile_options(spreadsheet_analyzer PRIVATE
	${PROJECT_WARNINGS}
	-Werror
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
include(expected)
include(uuid)
include(roboto_sans)
include(roboto_mono)

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
	expected
	stduuid
	roboto_sans
	roboto_mono
)
target_include_directories(spreadsheet_analyzer PRIVATE ${OPENGL_INCLUDE_DIR})
