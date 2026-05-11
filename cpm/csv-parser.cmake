include(CPM)
CPMAddPackage(
  NAME csv-parser
  VERSION 5.1.0
  URL https://github.com/vincentlaucsb/csv-parser/archive/refs/tags/5.1.0.zip
  URL_HASH SHA256=80493d1950cf33ba60151a64361b68f5a770ddb5dd67aa312b1f16a72ed03cc9
)

if (csv-parser_ADDED)
  set_target_properties(csv PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:csv,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
