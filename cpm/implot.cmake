include(CPM)
CPMAddPackage(
  NAME implot_external
  VERSION 0.16
  URL https://github.com/epezent/implot/archive/refs/tags/v0.16.zip
  URL_HASH MD5=0529747ea75870e9c2f7648581a55583
)

if (implot_external_ADDED)
  add_library(implot
    ${implot_external_SOURCE_DIR}/implot.cpp
    ${implot_external_SOURCE_DIR}/implot_items.cpp
  )

  target_include_directories(implot PUBLIC ${implot_external_SOURCE_DIR})
  target_link_libraries(implot PRIVATE imgui)
  target_compile_definitions(implot PUBLIC -DIMPLOT_DISABLE_OBSOLETE_FUNCTIONS=1)

  set_target_properties(implot PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:implot,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
