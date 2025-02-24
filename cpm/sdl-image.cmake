include(CPM)
CPMAddPackage(
  NAME sdl_image
  VERSION 3.2.0
  URL https://github.com/libsdl-org/SDL_image/archive/refs/tags/release-3.2.0.zip
  URL_HASH MD5=f1a86bb4224972c6e3d6ca18f6cdf99c
  OVERRIDE_FIND_PACKAGE
  OPTIONS
    "SDLIMAGE_BUILD_SHARED_LIBS OFF"
)

FetchContent_MakeAvailable(sdl_image)