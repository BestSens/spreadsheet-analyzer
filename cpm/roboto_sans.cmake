include(CPM)
CPMAddPackage(
  NAME roboto_sans
  URL https://github.com/mobiledesres/Google-UI-fonts/raw/refs/heads/main/zip/Roboto.zip
  URL_HASH MD5=197732d0bb0a64a8ba2e1f619767827c
  DOWNLOAD_ONLY YES
)

if (roboto_sans_ADDED)
	add_custom_command(
		OUTPUT roboto_sans.c
		COMMAND ${CMAKE_CURRENT_BINARY_DIR}/binary_to_compressed
			-nostatic
			${roboto_sans_SOURCE_DIR}/Roboto-Regular.ttf
			font_roboto_sans
			> roboto_sans.c
		DEPENDS binary_to_compressed
	)

	add_library(roboto_sans STATIC roboto_sans.c)
endif()
