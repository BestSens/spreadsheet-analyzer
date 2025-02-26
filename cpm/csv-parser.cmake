include(CPM)
CPMAddPackage(
  NAME csv-parser
  VERSION 2.3.0
  URL https://github.com/vincentlaucsb/csv-parser/archive/refs/tags/2.3.0.zip
  URL_HASH MD5=60af23d9431b9ce29eefb13040485027
  OVERRIDE_FIND_PACKAGE
)

find_package(csv-parser)
set_target_properties(csv PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:csv,INTERFACE_INCLUDE_DIRECTORIES>)