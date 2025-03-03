#include <stddef.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif

#ifdef __has_embed
const unsigned char icon_data[] = {
#embed "../assets/2025_02_ICON_shade.png"
};

const unsigned char logo_data[] = {
#embed "../assets/logo_4c.png"
};
#else
#include "2025_02_ICON_shade.png.h"
#include "logo_4c.png.h"
#endif

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

const size_t icon_data_size = sizeof(icon_data);
const size_t logo_data_size = sizeof(logo_data);
