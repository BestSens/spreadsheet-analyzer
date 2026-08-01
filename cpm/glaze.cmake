include(CPM)
CPMAddPackage(
  NAME glaze
  VERSION 7.9.1
  URL https://github.com/stephenberry/glaze/archive/refs/tags/v7.9.1.zip
  URL_HASH SHA256=3014214abb2518ffd843e4396d783083812acf993b3bca6f5df78e676d74dd52
  OPTIONS
    "glaze_DEVELOPER_MODE OFF"
    "glaze_BUILD_EXAMPLES OFF"
)

if (glaze_ADDED)
  set_target_properties(glaze_glaze PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:glaze_glaze,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
