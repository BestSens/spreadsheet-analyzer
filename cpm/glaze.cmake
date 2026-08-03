include(CPM)
CPMAddPackage(
  NAME glaze
  VERSION 8.0.0
  URL https://github.com/stephenberry/glaze/archive/refs/tags/v8.0.0.zip
  URL_HASH SHA256=772a8e9ea9e9a9e4eeb995c0bdbcbbc7e37faea5f6942bbd7b8d74b029963029
  OPTIONS
    "glaze_DEVELOPER_MODE OFF"
    "glaze_BUILD_EXAMPLES OFF"
)

if (glaze_ADDED)
  set_target_properties(glaze_glaze PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:glaze_glaze,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
