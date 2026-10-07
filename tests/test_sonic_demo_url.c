#include "sonic_core.h"

#include <assert.h>
#include <string.h>

#ifndef CONFIG_SONIC_DEMO_URL
#error "The regression must compile the configured SONIC_DEMO_URL value."
#endif

int main(void)
{
    static const uint8_t url[] = CONFIG_SONIC_DEMO_URL;
    uint8_t frames[SONIC_MAX_FRAGMENTS][SONIC_FRAME_SIZE];
    size_t frame_count = 0u;
    const size_t length = sizeof(url) - 1u;
    const size_t expected = (length + SONIC_FRAME_PAYLOAD_SIZE - 1u) /
                            SONIC_FRAME_PAYLOAD_SIZE;

    assert(length > 0u && length <= SONIC_MAX_MESSAGE_BYTES);
    assert(sonic_payload_validate(SONIC_TYPE_URL, url, length) == SONIC_OK);
    assert(sonic_message_fragment(SONIC_TYPE_URL, 0x534Cu, url, length,
                                  frames, SONIC_MAX_FRAGMENTS,
                                  &frame_count) == SONIC_OK);
    assert(frame_count == expected);
    assert(frame_count >= 1u && frame_count <= SONIC_MAX_FRAGMENTS);
    return 0;
}
