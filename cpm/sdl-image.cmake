include(CPM)
include(sdl)
CPMAddPackage(
  NAME sdl_image
  VERSION 3.4.4
  URL https://github.com/libsdl-org/SDL_image/archive/refs/tags/release-3.4.4.zip
  URL_HASH SHA256=35572365479e028cc6a43b3497edcd6c9366510be68da90a1b67947212ed0628
  OPTIONS
    "BUILD_SHARED_LIBS OFF"
)

if (sdl_image_ADDED)
  set_target_properties(SDL3_image-static PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
  )
  set_target_properties(SDL3_image-static PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:SDL3_image-static,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
