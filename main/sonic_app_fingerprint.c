#include "sonic_app_fingerprint.h"

static const char kHex[] = "0123456789ABCDEF";

void sonic_app_fingerprint_format(const uint8_t sha_prefix[4], uint8_t wire[4],
                                  char printable[9])
{
    for (unsigned int i = 0u; i < 4u; ++i) {
        wire[i] = sha_prefix != 0 ? sha_prefix[i] : 0u;
        printable[i * 2u] = kHex[wire[i] >> 4];
        printable[i * 2u + 1u] = kHex[wire[i] & 0x0Fu];
    }
    printable[8] = '\0';
}
