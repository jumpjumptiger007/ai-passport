#ifndef SONIC_APP_FINGERPRINT_H
#define SONIC_APP_FINGERPRINT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void sonic_app_fingerprint_format(const uint8_t sha_prefix[4], uint8_t wire[4],
                                  char printable[9]);

#ifdef __cplusplus
}
#endif

#endif
