include(CPM)
CPMAddPackage(
  NAME date_external
  VERSION 3.0.4
  URL https://github.com/HowardHinnant/date/archive/refs/tags/v3.0.4.zip
  URL_HASH SHA256=a6352ebdb440274269c746e6f66690f62ef4ab4aefeeba2ef2efee1bc2c86985
  DOWNLOAD_ONLY YES
)

if (NOT TARGET date)
  add_library(date INTERFACE)
  target_include_directories(date SYSTEM INTERFACE ${date_external_SOURCE_DIR}/include)
endif()
