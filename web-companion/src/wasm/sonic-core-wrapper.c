#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "sonic_core.h"

int sl_validate_payload(uint32_t type, const uint8_t *bytes, uint32_t length) {
    return (int)sonic_payload_validate((sonic_payload_type_t)type, bytes, length);
}

int sl_fragment(uint32_t type, uint16_t message_id, const uint8_t *bytes,
                uint32_t length, uint8_t *frames) {
    size_t count = 0;
    sonic_error_t err = sonic_message_fragment((sonic_payload_type_t)type,
                                               message_id, bytes, length,
                                               (uint8_t (*)[SONIC_FRAME_SIZE])frames,
                                               SONIC_MAX_FRAGMENTS, &count);
    return err == SONIC_OK ? (int)count : -(int)err;
}

void sl_reassembly_reset(sonic_reassembly_t *state) {
    if (state != NULL) sonic_reassembly_reset(state);
}

sonic_reassembly_t *sl_reassembly_create(void) {
    sonic_reassembly_t *state = (sonic_reassembly_t *)malloc(sizeof(*state));
    if (state != NULL) sonic_reassembly_reset(state);
    return state;
}

void sl_reassembly_free(sonic_reassembly_t *state) {
    free(state);
}

int sl_reassembly_accept(sonic_reassembly_t *state, const uint8_t *frame,
                         uint32_t now_ms, uint8_t *out) {
    sonic_reassembly_result_t result;
    memset(&result, 0, sizeof(result));
    sonic_reassembly_status_t status = sonic_reassembly_accept(
        state, frame, SONIC_FRAME_SIZE, now_ms, &result);
    out[0] = (uint8_t)status;
    out[1] = (uint8_t)result.error;
    out[2] = (uint8_t)(result.message_id & 0xff);
    out[3] = (uint8_t)(result.message_id >> 8);
    out[4] = (uint8_t)result.type;
    out[5] = result.received_fragments;
    out[6] = result.fragment_count;
    out[7] = result.message_length;
    memcpy(out + 8, result.payload, SONIC_MAX_MESSAGE_BYTES);
    return (int)status;
}

int sl_reassembly_poll(sonic_reassembly_t *state, uint32_t now_ms, uint8_t *out) {
    sonic_reassembly_result_t result;
    memset(&result, 0, sizeof(result));
    sonic_reassembly_status_t status = sonic_reassembly_poll(state, now_ms, &result);
    out[0] = (uint8_t)status;
    out[1] = (uint8_t)result.error;
    out[2] = (uint8_t)(result.message_id & 0xff);
    out[3] = (uint8_t)(result.message_id >> 8);
    out[4] = (uint8_t)result.type;
    out[5] = result.received_fragments;
    out[6] = result.fragment_count;
    out[7] = result.message_length;
    memcpy(out + 8, result.payload, SONIC_MAX_MESSAGE_BYTES);
    return (int)status;
}
