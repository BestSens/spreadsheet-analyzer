include(CPM)
CPMAddPackage(
  NAME fontawesome
  URL https://github.com/FortAwesome/Font-Awesome/raw/refs/heads/6.x/webfonts/fa-regular-400.ttf
  URL_HASH MD5=1564b0c2e86c525600499dae390a1056
  DOWNLOAD_ONLY YES
  DOWNLOAD_NO_EXTRACT YES
)

CPMAddPackage(
  NAME IconFontCppHeaders
  GITHUB_REPOSITORY "juliettef/IconFontCppHeaders"
  GIT_TAG "main"
  DOWNLOAD_ONLY YES
)

if (fontawesome_ADDED)
	add_custom_command(
		OUTPUT fontawesome.c
		COMMAND ${CMAKE_CURRENT_BINARY_DIR}/binary_to_compressed
			-nostatic
			${fontawesome_SOURCE_DIR}/fa-regular-400.ttf
			font_fontawesome
			> fontawesome.c
		DEPENDS binary_to_compressed
	)

	add_library(fontawesome STATIC fontawesome.c)

	if (IconFontCppHeaders_ADDED)
		target_include_directories(fontawesome PUBLIC ${IconFontCppHeaders_SOURCE_DIR})
	endif()
endif()
