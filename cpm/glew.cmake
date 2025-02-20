include(CPM)
CPMAddPackage(
  NAME glew
  VERSION 2.2.0
  URL https://github.com/nigels-com/glew/archive/refs/tags/glew-2.2.0.zip
  URL_HASH MD5=f150f61074d049ff0423b09b18cd1ef6
  OVERRIDE_FIND_PACKAGE
)

FetchContent_MakeAvailable(glew)