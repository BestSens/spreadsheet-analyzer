include(CPM)
CPMAddPackage(
  NAME csv-parser
  VERSION 5.3.0
  URL https://github.com/vincentlaucsb/csv-parser/archive/refs/tags/5.3.0.zip
  URL_HASH SHA256=2f4cf2b824a3aca3b9946ea8326b71ba99ebb055e8250f2f1a1cee203799cb3e
)

if (csv-parser_ADDED)
  set_target_properties(csv PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:csv,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
