include(CPM)
CPMAddPackage(
  NAME SDL3
  VERSION 3.4.8
  URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.8.zip
  URL_HASH SHA256=9ff97fce9ffb64abd071de3e177a26fa6c9f1283a56ed166889345666ba3ef9b
  OPTIONS
    "SDL_STATIC ON"
    "SDL_SHARED OFF"
)

if (SDL3_ADDED)
  set_target_properties(SDL3-static PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:SDL3-static,INTERFACE_INCLUDE_DIRECTORIES>)
endif()