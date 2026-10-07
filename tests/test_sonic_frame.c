#include "sonic_core.h"
#include "sonic_golden_vectors.h"

#include <assert.h>
#include <string.h>

static const sonic_golden_vector_t *find_vector(const char *name)
{
    for (size_t i = 0; i < sonic_golden_vector_count; ++i) {
        if (strcmp(sonic_golden_vectors[i].name, name) == 0) {
            return &sonic_golden_vectors[i];
        }
    }
    return NULL;
}

static void set_crc(uint8_t frame[SONIC_FRAME_SIZE])
{
    uint16_t crc = sonic_core_crc16(frame, SONIC_FRAME_CRC_OFFSET);
    frame[SONIC_FRAME_CRC_OFFSET] = (uint8_t) (crc >> 8);
    frame[SONIC_FRAME_CRC_OFFSET + 1u] = (uint8_t) crc;
}

int main(void)
{
    static const uint8_t crc_input[] = "123456789";
    uint8_t frame[SONIC_FRAME_SIZE];
    sonic_frame_t decoded;

    assert(sonic_core_crc16(crc_input, sizeof(crc_input) - 1u) == 0x29B1u);
    for (size_t vector_index = 0; vector_index < sonic_golden_vector_count;
         ++vector_index) {
        const sonic_golden_vector_t *vector = &sonic_golden_vectors[vector_index];
        uint8_t encoded[SONIC_MAX_FRAGMENTS][SONIC_FRAME_SIZE];
        size_t encoded_count = 0u;

        assert(sonic_message_fragment(vector->type, vector->message_id,
                                      vector->payload, vector->payload_length,
                                      encoded, SONIC_MAX_FRAGMENTS,
                                      &encoded_count) == SONIC_OK);
        assert(encoded_count == vector->frame_count);
        for (size_t frame_index = 0; frame_index < vector->frame_count;
             ++frame_index) {
            assert(memcmp(encoded[frame_index], vector->frames[frame_index],
                          SONIC_FRAME_SIZE) == 0);
            assert(sonic_frame_decode(vector->frames[frame_index],
                                      SONIC_FRAME_SIZE, &decoded) == SONIC_OK);
            assert(decoded.type == vector->type);
            assert(decoded.message_id == vector->message_id);
            assert(decoded.fragment_index == frame_index);
            assert(decoded.fragment_count == vector->frame_count);
        }
    }

    assert(sonic_error_name(SONIC_BAD_CRC) != NULL);
    assert(strcmp(sonic_error_name(SONIC_BAD_CRC), "BAD_CRC") == 0);
    assert(strcmp(sonic_error_name((sonic_error_t) 255), "UNKNOWN_ERROR") == 0);

    memcpy(frame, find_vector("empty_text")->frames[0], sizeof(frame));
    assert(sonic_frame_decode(frame, SONIC_FRAME_SIZE - 1u, &decoded) ==
           SONIC_BAD_LENGTH);
    frame[0] ^= 0x01u;
    assert(sonic_frame_decode(frame, sizeof(frame), &decoded) == SONIC_BAD_MAGIC);

    memcpy(frame, find_vector("empty_text")->frames[0], sizeof(frame));
    frame[2] = 0x21u;
    assert(sonic_frame_decode(frame, sizeof(frame), &decoded) == SONIC_BAD_VERSION);
    frame[2] = 0x15u;
    assert(sonic_frame_decode(frame, sizeof(frame), &decoded) == SONIC_UNKNOWN_TYPE);

    memcpy(frame, find_vector("empty_text")->frames[0], sizeof(frame));
    frame[3] = 0u;
    frame[4] = 0u;
    assert(sonic_frame_decode(frame, sizeof(frame), &decoded) ==
           SONIC_BAD_MESSAGE_ID);
    frame[3] = 0u;
    frame[4] = 1u;
    frame[5] = 0x03u;
    assert(sonic_frame_decode(frame, sizeof(frame), &decoded) == SONIC_BAD_FRAGMENT);
    frame[5] = 0u;
    frame[6] = SONIC_FRAME_PAYLOAD_SIZE + 1u;
    assert(sonic_frame_decode(frame, sizeof(frame), &decoded) == SONIC_BAD_LENGTH);

    memcpy(frame, find_vector("empty_text")->frames[0], sizeof(frame));
    frame[7] = 0x01u;
    set_crc(frame);
    assert(sonic_frame_decode(frame, sizeof(frame), &decoded) == SONIC_BAD_PADDING);

    memcpy(frame, find_vector("empty_text")->frames[0], sizeof(frame));
    frame[SONIC_FRAME_CRC_OFFSET + 1u] ^= 0x01u;
    assert(sonic_frame_decode(frame, sizeof(frame), &decoded) == SONIC_BAD_CRC);

    {
        const uint8_t malformed_utf8[] = {0xC0u};
        assert(sonic_frame_encode(frame, sizeof(frame), SONIC_TYPE_TEXT, 1u,
                                  0u, 1u, malformed_utf8,
                                  sizeof(malformed_utf8)) == SONIC_OK);
        assert(sonic_frame_decode(frame, sizeof(frame), &decoded) ==
               SONIC_INVALID_UTF8);
    }
    {
        static const uint8_t unsupported_url[] = "javascript:alert(1)";
        assert(sonic_frame_encode(frame, sizeof(frame), SONIC_TYPE_URL, 1u,
                                  0u, 1u, unsupported_url,
                                  sizeof(unsupported_url) - 1u) == SONIC_OK);
        assert(sonic_frame_decode(frame, sizeof(frame), &decoded) ==
               SONIC_INVALID_URL);
    }

    return 0;
}
