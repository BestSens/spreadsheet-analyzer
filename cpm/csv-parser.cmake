include(CPM)
CPMAddPackage(
  NAME csv-parser
  VERSION 5.2.0
  URL https://github.com/vincentlaucsb/csv-parser/archive/refs/tags/5.2.0.zip
  URL_HASH SHA256=5ad581b8cff069155dde82673ae52c08e9219430c17cd44581d6411cc62ef26f
)

if (csv-parser_ADDED)
  set_target_properties(csv PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:csv,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
