cmake_minimum_required(VERSION 3.24)

project(spreadsheet-analyzer-2 VERSION 2.0.0)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cpm")

add_executable(spreadsheet_analyzer
	src/main.cpp
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
include(glew)
include(fmt)
include(spdlog)

target_link_libraries(spreadsheet_analyzer PRIVATE imgui SDL3::SDL3 Threads::Threads ${OPENGL_LIBRARIES} fmt spdlog)
target_include_directories(spreadsheet_analyzer PRIVATE ${OPENGL_INCLUDE_DIR})