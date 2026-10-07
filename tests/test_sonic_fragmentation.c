#include "sonic_core.h"
#include "sonic_golden_vectors.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    static const size_t lengths[] = {0u, 1u, 31u, 32u, 62u, 63u, 92u, 93u};
    uint8_t payload[SONIC_MAX_MESSAGE_BYTES];
    uint8_t first[SONIC_MAX_FRAGMENTS][SONIC_FRAME_SIZE];
    uint8_t again[SONIC_MAX_FRAGMENTS][SONIC_FRAME_SIZE];

    for (size_t i = 0; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t) ('a' + (i % 26u));
    }
    for (size_t test = 0; test < sizeof(lengths) / sizeof(lengths[0]); ++test) {
        size_t frame_count = 0u;
        size_t again_count = 0u;
        size_t expected = lengths[test] == 0u
                              ? 1u
                              : (lengths[test] + SONIC_FRAME_PAYLOAD_SIZE - 1u) /
                                    SONIC_FRAME_PAYLOAD_SIZE;
        assert(sonic_message_fragment(SONIC_TYPE_TOKEN, 0xCAFEu, payload,
                                      lengths[test], first,
                                      SONIC_MAX_FRAGMENTS,
                                      &frame_count) == SONIC_OK);
        assert(frame_count == expected);
        assert(sonic_message_fragment(SONIC_TYPE_TOKEN, 0xCAFEu, payload,
                                      lengths[test], again,
                                      SONIC_MAX_FRAGMENTS,
                                      &again_count) == SONIC_OK);
        assert(again_count == frame_count);
        assert(memcmp(first, again, frame_count * SONIC_FRAME_SIZE) == 0);
        for (size_t index = 0; index < frame_count; ++index) {
            sonic_frame_t decoded;
            size_t offset = index * SONIC_FRAME_PAYLOAD_SIZE;
            size_t remaining = lengths[test] - offset;
            size_t expected_chunk = remaining > SONIC_FRAME_PAYLOAD_SIZE
                                        ? SONIC_FRAME_PAYLOAD_SIZE
                                        : remaining;
            assert(sonic_frame_decode(first[index], SONIC_FRAME_SIZE,
                                      &decoded) == SONIC_OK);
            assert(decoded.fragment_index == index);
            assert(decoded.fragment_count == expected);
            assert(decoded.chunk_length == expected_chunk);
        }
    }

    {
        size_t frame_count = 99u;
        assert(sonic_message_fragment(SONIC_TYPE_TOKEN, 1u, payload, 94u,
                                      first, SONIC_MAX_FRAGMENTS,
                                      &frame_count) == SONIC_BAD_LENGTH);
        assert(frame_count == 0u);
        assert(sonic_message_fragment(SONIC_TYPE_TOKEN, 1u, payload, 93u,
                                      first, 2u,
                                      &frame_count) == SONIC_BUFFER_TOO_SMALL);
        assert(frame_count == 0u);
        assert(sonic_message_fragment(SONIC_TYPE_TOKEN, 0u, payload, 1u,
                                      first, SONIC_MAX_FRAGMENTS,
                                      &frame_count) == SONIC_BAD_MESSAGE_ID);
    }

    assert(sonic_message_id_normalize(0u) == 1u);
    assert(sonic_message_id_normalize(0xFFFFu) == 0xFFFFu);
    assert(sonic_message_id_next(0x0000u) == 1u);
    assert(sonic_message_id_next(0xFFFEu) == 0xFFFFu);
    assert(sonic_message_id_next(0xFFFFu) == 1u);

    {
        static const uint8_t bad_utf8[] = {0xF0u, 0x80u, 0x80u, 0x80u};
        assert(sonic_payload_validate(SONIC_TYPE_TEXT, bad_utf8,
                                      sizeof(bad_utf8)) == SONIC_INVALID_UTF8);
        assert(sonic_payload_validate(SONIC_TYPE_TOKEN, bad_utf8,
                                      sizeof(bad_utf8)) == SONIC_OK);
    }

    {
        static const uint8_t http_url[] = "http://example.com/x";
        uint8_t frames[SONIC_MAX_FRAGMENTS][SONIC_FRAME_SIZE];
        size_t count = 0u;
        assert(sonic_payload_validate(SONIC_TYPE_URL, http_url,
                                      sizeof(http_url) - 1u) == SONIC_OK);
        assert(sonic_message_fragment(SONIC_TYPE_URL, 0x2345u, http_url,
                                      sizeof(http_url) - 1u, frames,
                                      SONIC_MAX_FRAGMENTS, &count) == SONIC_OK);
        assert(count == 1u);
    }

    {
        uint8_t oversized_url[94];
        size_t count = 0u;
        memcpy(oversized_url, "https://example.com/", 20u);
        memset(oversized_url + 20u, 'x', sizeof(oversized_url) - 20u);
        assert(sonic_payload_validate(SONIC_TYPE_URL, oversized_url,
                                      sizeof(oversized_url)) == SONIC_BAD_LENGTH);
        assert(sonic_message_fragment(SONIC_TYPE_URL, 0x3456u, oversized_url,
                                      sizeof(oversized_url), first,
                                      SONIC_MAX_FRAGMENTS, &count) ==
               SONIC_BAD_LENGTH);
        assert(count == 0u);
    }

    {
        static const uint8_t byte_classes[] = {
            0x00u, 0x01u, 0x1Fu, 0x20u, 0x7Fu, 0x80u, 0xC0u, 0xFEu, 0xFFu
        };
        uint8_t binary_token[SONIC_MAX_MESSAGE_BYTES];
        size_t token_frame_count = 0u;
        for (size_t i = 0; i < sizeof(binary_token); ++i) {
            binary_token[i] = byte_classes[i % sizeof(byte_classes)];
        }
        assert(sonic_payload_validate(SONIC_TYPE_TOKEN, binary_token,
                                      sizeof(binary_token)) == SONIC_OK);
        assert(sonic_message_fragment(SONIC_TYPE_TOKEN, 0x4567u, binary_token,
                                      sizeof(binary_token), first,
                                      SONIC_MAX_FRAGMENTS,
                                      &token_frame_count) ==
               SONIC_OK);
        assert(token_frame_count == SONIC_MAX_FRAGMENTS);
        for (size_t i = 0; i < token_frame_count; ++i) {
            sonic_frame_t decoded;
            assert(sonic_frame_decode(first[i], SONIC_FRAME_SIZE,
                                      &decoded) == SONIC_OK);
        }
    }
    return 0;
}
