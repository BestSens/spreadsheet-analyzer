include(CPM)
CPMAddPackage(
  NAME SDL3
  VERSION 3.4.12
  URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.12.zip
  URL_HASH SHA256=ce4e4b92e628b376b59091fcaa7358044f4c7009b6d35be0d81abe8e1de7847a
  OPTIONS
    "SDL_STATIC ON"
    "SDL_SHARED OFF"
)

if (SDL3_ADDED)
  set_target_properties(SDL3-static PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:SDL3-static,INTERFACE_INCLUDE_DIRECTORIES>)
endif()