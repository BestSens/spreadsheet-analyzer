include(CPM)
CPMAddPackage(
  NAME spdlog
  VERSION 1.15.1
  OPTIONS
    "SPDLOG_FMT_EXTERNAL ON"
  URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.1.tar.gz
  URL_HASH MD5=3a8f758489a1bf21403eabf49e78c3a4
)
