include(CPM)
CPMAddPackage(
  NAME stduuid
  VERSION 1.2.3
  URL https://github.com/mariusbancila/stduuid/archive/refs/tags/v1.2.3.zip
  URL_HASH MD5=32a2c2d1b979178c71c439cf0481a3d9
  OVERRIDE_FIND_PACKAGE
)

FetchContent_MakeAvailable(stduuid)

set_target_properties(stduuid PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:stduuid,INTERFACE_INCLUDE_DIRECTORIES>)
