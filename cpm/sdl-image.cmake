include(CPM)
include(sdl)
CPMAddPackage(
  NAME sdl_image
  VERSION 3.2.0
  URL https://github.com/libsdl-org/SDL_image/archive/refs/tags/release-3.2.0.zip
  URL_HASH MD5=f1a86bb4224972c6e3d6ca18f6cdf99c
  OPTIONS
    "BUILD_SHARED_LIBS OFF"
)

if (sdl_image_ADDED)
  set_target_properties(SDL3_image-static PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:SDL3_image-static,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
