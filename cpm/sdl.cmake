include(CPM)
CPMAddPackage(
  NAME SDL3
  VERSION 3.2.4
  URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.2.4.zip
  URL_HASH MD5=08e50712661af53f7f260b5f1efb85e1
  OVERRIDE_FIND_PACKAGE
  OPTIONS
    "SDL_STATIC ON"
    "SDL_SHARED OFF"
)

FetchContent_MakeAvailable(SDL3)

set_target_properties(SDL3-static PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:SDL3-static,INTERFACE_INCLUDE_DIRECTORIES>)
