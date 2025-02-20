include(CPM)
CPMAddPackage(
  NAME spdlog
  VERSION 1.15.0
  OPTIONS
    "SPDLOG_FMT_EXTERNAL ON"
  URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.0.tar.gz
  URL_HASH MD5=822878647de9eacfe43695a674fb551f
  OVERRIDE_FIND_PACKAGE
)

find_package(spdlog)