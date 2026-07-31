include(CPM)
CPMAddPackage(
  NAME FastFloat
  VERSION 8.2.10
  URL https://github.com/fastfloat/fast_float/archive/refs/tags/v8.2.10.zip
  URL_HASH SHA256=b19773c2f8f488fe67307a29654a2441e98d31f9b6c2b83109cf79a75bcafd19
)

if (FastFloat_ADDED)
  set_target_properties(fast_float PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:fast_float,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
