include(CPM)
CPMAddPackage(
  NAME expected
  VERSION 1.1
  URL https://github.com/TartanLlama/expected/archive/refs/tags/v1.1.0.zip
  URL_HASH MD5=cbc9465bb0e9328c821fc3cf89ec7711
  OVERRIDE_FIND_PACKAGE
)

FetchContent_MakeAvailable(expected)

set_target_properties(expected PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:expected,INTERFACE_INCLUDE_DIRECTORIES>)
