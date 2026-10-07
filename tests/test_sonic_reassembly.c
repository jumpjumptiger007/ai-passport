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
    const sonic_golden_vector_t *multi = find_vector("multi_text");
    const sonic_golden_vector_t *single = find_vector("text_a");
    const sonic_golden_vector_t *triple = find_vector("max_ascii_text");
    sonic_reassembly_t state;
    sonic_reassembly_result_t result;

    assert(single != NULL && single->frame_count == 1u);
    sonic_reassembly_reset(&state);
    assert(sonic_reassembly_accept(&state, single->frames[0], SONIC_FRAME_SIZE,
                                   50u, &result) ==
           SONIC_REASSEMBLY_COMPLETE);
    assert(result.message_id == single->message_id);
    assert(result.type == single->type);
    assert(result.message_length == single->payload_length);
    assert(memcmp(result.payload, single->payload, single->payload_length) == 0);

    assert(multi != NULL && multi->frame_count == 2u);
    sonic_reassembly_reset(&state);
    assert(sonic_reassembly_accept(&state, multi->frames[0], SONIC_FRAME_SIZE,
                                   100u, &result) ==
           SONIC_REASSEMBLY_IN_PROGRESS);
    assert(result.received_fragments == 1u && result.message_length == 31u);
    assert(sonic_reassembly_accept(&state, multi->frames[0], SONIC_FRAME_SIZE,
                                   200u, &result) ==
           SONIC_REASSEMBLY_DUPLICATE);
    assert(result.received_fragments == 1u);
    assert(sonic_reassembly_accept(&state, multi->frames[1], SONIC_FRAME_SIZE,
                                   300u, &result) ==
           SONIC_REASSEMBLY_COMPLETE);
    assert(result.message_length == multi->payload_length);
    assert(result.message_id == multi->message_id && result.type == multi->type);
    assert(memcmp(result.payload, multi->payload, multi->payload_length) == 0);
    assert(state.active == 0u);

    assert(triple != NULL && triple->frame_count == 3u);
    sonic_reassembly_reset(&state);
    for (size_t i = 0; i < triple->frame_count; ++i) {
        sonic_reassembly_status_t expected =
            i + 1u == triple->frame_count ? SONIC_REASSEMBLY_COMPLETE
                                          : SONIC_REASSEMBLY_IN_PROGRESS;
        assert(sonic_reassembly_accept(&state, triple->frames[i],
                                       SONIC_FRAME_SIZE,
                                       (uint32_t) (400u + i * 100u),
                                       &result) == expected);
    }
    assert(result.message_id == triple->message_id);
    assert(result.type == triple->type);
    assert(result.message_length == triple->payload_length);
    assert(memcmp(result.payload, triple->payload, triple->payload_length) == 0);

    {
        uint8_t text[33];
        uint8_t frames[SONIC_MAX_FRAGMENTS][SONIC_FRAME_SIZE];
        size_t frame_count = 0u;
        memset(text, 'a', 30u);
        text[30] = 0xE2u;
        text[31] = 0x82u;
        text[32] = 0xACu;
        assert(sonic_payload_validate(SONIC_TYPE_TEXT, text, sizeof(text)) ==
               SONIC_OK);
        assert(sonic_message_fragment(SONIC_TYPE_TEXT, 0xABCDu, text,
                                      sizeof(text), frames,
                                      SONIC_MAX_FRAGMENTS,
                                      &frame_count) == SONIC_OK);
        assert(frame_count == 2u);
        {
            sonic_frame_t decoded;
            assert(frames[0][7u + SONIC_FRAME_PAYLOAD_SIZE - 1u] == 0xE2u);
            assert(sonic_frame_decode(frames[0], SONIC_FRAME_SIZE,
                                      &decoded) == SONIC_OK);
        }
        sonic_reassembly_reset(&state);
        assert(sonic_reassembly_accept(&state, frames[0], SONIC_FRAME_SIZE,
                                       700u, &result) ==
               SONIC_REASSEMBLY_IN_PROGRESS);
        assert(sonic_reassembly_accept(&state, frames[1], SONIC_FRAME_SIZE,
                                       800u, &result) ==
               SONIC_REASSEMBLY_COMPLETE);
        assert(result.message_id == 0xABCDu);
        assert(result.type == SONIC_TYPE_TEXT);
        assert(result.message_length == sizeof(text));
        assert(memcmp(result.payload, text, sizeof(text)) == 0);
    }

    {
        static const uint8_t raw_text[] = {'A', '\r', '\n', 'B', '\r', 'C'};
        static const uint8_t normalized_text[] = {'A', '\n', 'B', '\n', 'C'};
        uint8_t frames[SONIC_MAX_FRAGMENTS][SONIC_FRAME_SIZE];
        size_t count = 0u;

        /* Sender adapters normalize CRLF/CR to LF before calling sonic_core. */
        sonic_reassembly_reset(&state);
        assert(sonic_message_fragment(SONIC_TYPE_TEXT, 0xBCDEu,
                                      normalized_text,
                                      sizeof(normalized_text), frames,
                                      SONIC_MAX_FRAGMENTS, &count) == SONIC_OK);
        assert(sonic_reassembly_accept(&state, frames[0], SONIC_FRAME_SIZE,
                                       900u, &result) ==
               SONIC_REASSEMBLY_COMPLETE);
        assert(result.message_length == sizeof(normalized_text));
        assert(memcmp(result.payload, normalized_text,
                      sizeof(normalized_text)) == 0);

        /* The transport core itself preserves the bytes supplied by its caller. */
        assert(sonic_message_fragment(SONIC_TYPE_TEXT, 0xBCDFu, raw_text,
                                      sizeof(raw_text), frames,
                                      SONIC_MAX_FRAGMENTS, &count) == SONIC_OK);
        assert(sonic_reassembly_accept(&state, frames[0], SONIC_FRAME_SIZE,
                                       1000u, &result) ==
               SONIC_REASSEMBLY_COMPLETE);
        assert(result.message_length == sizeof(raw_text));
        assert(memcmp(result.payload, raw_text, sizeof(raw_text)) == 0);
    }

    {
        static const uint8_t byte_classes[] = {
            0x00u, 0x01u, 0x1Fu, 0x20u, 0x7Fu, 0x80u, 0xC0u, 0xFEu, 0xFFu
        };
        uint8_t token[SONIC_MAX_MESSAGE_BYTES];
        uint8_t frames[SONIC_MAX_FRAGMENTS][SONIC_FRAME_SIZE];
        size_t frame_count = 0u;
        for (size_t i = 0; i < sizeof(token); ++i) {
            token[i] = byte_classes[i % sizeof(byte_classes)];
        }
        assert(sonic_message_fragment(SONIC_TYPE_TOKEN, 0xCDEFu, token,
                                      sizeof(token), frames,
                                      SONIC_MAX_FRAGMENTS,
                                      &frame_count) == SONIC_OK);
        assert(frame_count == 3u);
        sonic_reassembly_reset(&state);
        for (size_t i = 0; i < frame_count; ++i) {
            sonic_reassembly_status_t expected =
                i + 1u == frame_count ? SONIC_REASSEMBLY_COMPLETE
                                      : SONIC_REASSEMBLY_IN_PROGRESS;
            assert(sonic_reassembly_accept(&state, frames[i],
                                           SONIC_FRAME_SIZE,
                                           (uint32_t) (1100u + i * 100u),
                                           &result) == expected);
        }
        assert(result.message_id == 0xCDEFu);
        assert(result.type == SONIC_TYPE_TOKEN);
        assert(result.message_length == sizeof(token));
        assert(memcmp(result.payload, token, sizeof(token)) == 0);
    }

    sonic_reassembly_reset(&state);
    assert(sonic_reassembly_accept(&state, multi->frames[1], SONIC_FRAME_SIZE,
                                   100u, &result) == SONIC_REASSEMBLY_IGNORED);
    assert(sonic_reassembly_accept(&state, multi->frames[0], SONIC_FRAME_SIZE,
                                   200u, &result) ==
           SONIC_REASSEMBLY_IN_PROGRESS);
    assert(sonic_reassembly_accept(&state, multi->frames[1], SONIC_FRAME_SIZE,
                                   300u, &result) ==
           SONIC_REASSEMBLY_COMPLETE);

    sonic_reassembly_reset(&state);
    assert(sonic_reassembly_accept(&state, multi->frames[0], SONIC_FRAME_SIZE,
                                   100u, &result) ==
           SONIC_REASSEMBLY_IN_PROGRESS);
    {
        uint8_t conflicting[SONIC_FRAME_SIZE];
        memcpy(conflicting, multi->frames[0], sizeof(conflicting));
        conflicting[7] ^= 0x01u;
        set_crc(conflicting);
        assert(sonic_reassembly_accept(&state, conflicting, sizeof(conflicting),
                                       200u, &result) ==
               SONIC_REASSEMBLY_ERROR);
        assert(result.error == SONIC_FRAGMENT_CONFLICT);
        assert(state.active == 0u);
    }

    sonic_reassembly_reset(&state);
    assert(sonic_reassembly_accept(&state, multi->frames[0], SONIC_FRAME_SIZE,
                                   100u, &result) ==
           SONIC_REASSEMBLY_IN_PROGRESS);
    {
        uint8_t other_type[SONIC_FRAME_SIZE];
        static const uint8_t token_chunk[SONIC_FRAME_PAYLOAD_SIZE] = {1u};
        assert(sonic_frame_encode(other_type, sizeof(other_type),
                                  SONIC_TYPE_TOKEN, multi->message_id, 0u, 2u,
                                  token_chunk, sizeof(token_chunk)) == SONIC_OK);
        assert(sonic_reassembly_accept(&state, other_type, sizeof(other_type),
                                       200u, &result) ==
               SONIC_REASSEMBLY_ERROR);
        assert(result.error == SONIC_FRAGMENT_CONFLICT);
    }

    sonic_reassembly_reset(&state);
    assert(sonic_reassembly_accept(&state, multi->frames[0], SONIC_FRAME_SIZE,
                                   100u, &result) ==
           SONIC_REASSEMBLY_IN_PROGRESS);
    {
        uint8_t new_message[SONIC_MAX_FRAGMENTS][SONIC_FRAME_SIZE];
        size_t count = 0u;
        assert(sonic_message_fragment(SONIC_TYPE_TOKEN, 7u,
                                      find_vector("token_nul")->payload, 3u,
                                      new_message, SONIC_MAX_FRAGMENTS,
                                      &count) == SONIC_OK);
        assert(sonic_reassembly_accept(&state, new_message[0], SONIC_FRAME_SIZE,
                                       200u, &result) ==
               SONIC_REASSEMBLY_COMPLETE);
        assert(result.message_id == 7u && result.type == SONIC_TYPE_TOKEN);
    }

    sonic_reassembly_reset(&state);
    assert(sonic_reassembly_accept(&state, multi->frames[0], SONIC_FRAME_SIZE,
                                   100u, &result) ==
           SONIC_REASSEMBLY_IN_PROGRESS);
    assert(sonic_reassembly_poll(&state, 10099u, &result) ==
           SONIC_REASSEMBLY_IN_PROGRESS);
    assert(sonic_reassembly_poll(&state, 10100u, &result) ==
           SONIC_REASSEMBLY_INCOMPLETE);
    assert(result.error == SONIC_ASSEMBLY_TIMEOUT && state.active != 0u);
    assert(sonic_reassembly_accept(&state, multi->frames[1], SONIC_FRAME_SIZE,
                                   15000u, &result) ==
           SONIC_REASSEMBLY_COMPLETE);

    sonic_reassembly_reset(&state);
    assert(sonic_reassembly_accept(&state, multi->frames[0], SONIC_FRAME_SIZE,
                                   100u, &result) ==
           SONIC_REASSEMBLY_IN_PROGRESS);
    assert(sonic_reassembly_poll(&state, 30100u, &result) ==
           SONIC_REASSEMBLY_EXPIRED);
    assert(state.active == 0u);
    assert(sonic_reassembly_poll(&state, 30101u, &result) ==
           SONIC_REASSEMBLY_IDLE);

    return 0;
}
