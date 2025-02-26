#include <stddef.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
const unsigned char icon_data[] = {
#embed "../assets/2025_02_ICON_shade.png"
};

const unsigned char logo_data[] = {
#embed "../assets/logo_4c.png"
};
#pragma clang diagnostic pop

const size_t icon_data_size = sizeof(icon_data);
const size_t logo_data_size = sizeof(logo_data);
