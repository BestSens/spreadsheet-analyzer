include(CPM)
CPMAddPackage(
  NAME fmt
  VERSION 11.0.2
  URL https://github.com/fmtlib/fmt/releases/download/11.0.2/fmt-11.0.2.zip
  URL_HASH MD5=c622dca45ec3fc95254c48370a9f7a1d
  OVERRIDE_FIND_PACKAGE
)

find_package(fmt)