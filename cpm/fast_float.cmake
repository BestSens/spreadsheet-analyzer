include(CPM)
CPMAddPackage(
  NAME FastFloat
  VERSION 8.0.2
  URL https://github.com/fastfloat/fast_float/archive/refs/tags/v8.0.2.zip
  URL_HASH MD5=e1af637cfa7fe67ff5c74868270dc9a7
)

if (FastFloat_ADDED)
  set_target_properties(fast_float PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:fast_float,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
