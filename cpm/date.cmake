include(CPM)
CPMAddPackage(
  NAME date_external
  VERSION 3.0.5
  URL https://github.com/HowardHinnant/date/archive/refs/tags/v3.0.5.zip
  URL_HASH SHA256=10a18a056e6dab4cc8c20a4b3c54112e29ccaa5c389ade8fd7457a80c47b9c6d
  DOWNLOAD_ONLY YES
)

if (NOT TARGET date)
  add_library(date INTERFACE)
  target_include_directories(date SYSTEM INTERFACE ${date_external_SOURCE_DIR}/include)
endif()
