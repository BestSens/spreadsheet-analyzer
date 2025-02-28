#include "fonts.hpp"
#include "imgui.h"

extern "C" const unsigned int font_roboto_mono_compressed_size;
extern "C" const unsigned char font_roboto_mono_compressed_data[];
extern "C" const unsigned int font_roboto_sans_compressed_size;
extern "C" const unsigned char font_roboto_sans_compressed_data[];

namespace {
	// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
	bool fonts_initialized = false;
}

auto addFonts() -> void {
	auto &io = ImGui::GetIO();
	io.Fonts->AddFontFromMemoryCompressedTTF(static_cast<const void *>(font_roboto_sans_compressed_data),
											 static_cast<int>(font_roboto_sans_compressed_size), 16.0f);
	io.Fonts->AddFontFromMemoryCompressedTTF(static_cast<const void *>(font_roboto_mono_compressed_data),
											 static_cast<int>(font_roboto_mono_compressed_size), 16.0f);
	io.Fonts->AddFontFromMemoryCompressedTTF(static_cast<const void *>(font_roboto_mono_compressed_data),
											 static_cast<int>(font_roboto_mono_compressed_size), 20.0f);
	
	fonts_initialized = true;
}

auto getFont(fontList font) -> ImFont * {
	if (!fonts_initialized) {
		return nullptr;
	}

	switch (font) {
		case fontList::ROBOTO_SANS_16:
			return ImGui::GetIO().Fonts->Fonts[0];
		case fontList::ROBOTO_MONO_16:
			return ImGui::GetIO().Fonts->Fonts[1];
		case fontList::ROBOTO_MONO_20:
			return ImGui::GetIO().Fonts->Fonts[2];
		default:
			return nullptr;
	}
}