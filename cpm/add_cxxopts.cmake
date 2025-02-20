include(CPM)
CPMAddPackage(
  NAME cxxopts
  VERSION 3.2.0
  OPTIONS
    "CXXOPTS_BUILD_EXAMPLES OFF"
    "CXXOPTS_BUILD_TESTS OFF"
  URL https://github.com/jarro2783/cxxopts/archive/refs/tags/v3.2.0.tar.gz
  URL_HASH MD5=23226abcb20225d1033217c137c353d6
  OVERRIDE_FIND_PACKAGE
)

find_package(cxxopts)