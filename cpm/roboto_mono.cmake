include(CPM)
CPMAddPackage(
  NAME roboto_mono
  URL https://github.com/mobiledesres/Google-UI-fonts/raw/refs/heads/main/zip/Roboto%20Mono.zip
  URL_HASH MD5=384f62937cb6db402307dc535ac65c2c
  DOWNLOAD_ONLY YES
)

if (roboto_mono_ADDED)
	add_custom_command(
		OUTPUT roboto_mono.c
		COMMAND ${CMAKE_CURRENT_BINARY_DIR}/binary_to_compressed
			-nostatic
			${roboto_mono_SOURCE_DIR}/static/RobotoMono-Regular.ttf
			font_roboto_mono
			> roboto_mono.c
		DEPENDS binary_to_compressed
	)

	add_library(roboto_mono STATIC roboto_mono.c)
endif()
