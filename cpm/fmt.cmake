include(CPM)
CPMAddPackage(
  NAME fmt
  VERSION 11.1.4
  URL https://github.com/fmtlib/fmt/releases/download/11.1.4/fmt-11.1.4.zip
  URL_HASH MD5=ad6a56b15cddf4aad2a234e7cfc9e8c9
  OVERRIDE_FIND_PACKAGE
)

find_package(fmt)

set_target_properties(fmt PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:fmt,INTERFACE_INCLUDE_DIRECTORIES>)
