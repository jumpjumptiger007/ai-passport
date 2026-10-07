#include "sonic_core.h"

#include <string.h>

#define SONIC_MAGIC_0 0x53u
#define SONIC_MAGIC_1 0x4Cu
#define SONIC_KNOWN_TYPE_MASK 0x0Fu
#define SONIC_VERSION_SHIFT 4u
#define SONIC_RESERVED_CAPABILITY_MASK 0x80u

static int sonic_type_is_known(uint8_t type)
{
    return type >= (uint8_t) SONIC_TYPE_TEXT &&
           type <= (uint8_t) SONIC_TYPE_DEVICE_INFO;
}

static int sonic_utf8_is_valid(const uint8_t *bytes, size_t length)
{
    size_t i = 0;

    while (i < length) {
        uint8_t first = bytes[i];
        size_t extra = 0;
        uint32_t codepoint;

        if (first <= 0x7Fu) {
            ++i;
            continue;
        }
        if (first >= 0xC2u && first <= 0xDFu) {
            extra = 1;
            codepoint = first & 0x1Fu;
        } else if (first >= 0xE0u && first <= 0xEFu) {
            extra = 2;
            codepoint = first & 0x0Fu;
        } else if (first >= 0xF0u && first <= 0xF4u) {
            extra = 3;
            codepoint = first & 0x07u;
        } else {
            return 0;
        }

        if (length - i <= extra) {
            return 0;
        }
        for (size_t j = 1; j <= extra; ++j) {
            uint8_t next = bytes[i + j];
            if ((next & 0xC0u) != 0x80u) {
                return 0;
            }
            codepoint = (codepoint << 6) | (next & 0x3Fu);
        }

        if ((extra == 1 && codepoint < 0x80u) ||
            (extra == 2 && codepoint < 0x800u) ||
            (extra == 3 && codepoint < 0x10000u) ||
            (codepoint >= 0xD800u && codepoint <= 0xDFFFu) ||
            codepoint > 0x10FFFFu) {
            return 0;
        }
        i += extra + 1;
    }
    return 1;
}

static int sonic_ascii_equal_ci(uint8_t left, char right)
{
    if (left >= 'A' && left <= 'Z') {
        left = (uint8_t) (left + ('a' - 'A'));
    }
    return left == (uint8_t) right;
}

static int sonic_url_has_supported_scheme(const uint8_t *bytes, size_t length)
{
    size_t prefix_length;

    if (length >= 7 && sonic_ascii_equal_ci(bytes[0], 'h') &&
        sonic_ascii_equal_ci(bytes[1], 't') &&
        sonic_ascii_equal_ci(bytes[2], 't') &&
        sonic_ascii_equal_ci(bytes[3], 'p') && bytes[4] == ':' &&
        bytes[5] == '/' && bytes[6] == '/') {
        prefix_length = 7;
    } else if (length >= 8 && sonic_ascii_equal_ci(bytes[0], 'h') &&
               sonic_ascii_equal_ci(bytes[1], 't') &&
               sonic_ascii_equal_ci(bytes[2], 't') &&
               sonic_ascii_equal_ci(bytes[3], 'p') &&
               sonic_ascii_equal_ci(bytes[4], 's') && bytes[5] == ':' &&
               bytes[6] == '/' && bytes[7] == '/') {
        prefix_length = 8;
    } else {
        return 0;
    }

    if (prefix_length == length || bytes[prefix_length] == '/' ||
        bytes[prefix_length] == '?' || bytes[prefix_length] == '#') {
        return 0;
    }
    for (size_t i = 0; i < length; ++i) {
        if (bytes[i] <= 0x20u || bytes[i] == 0x7Fu) {
            return 0;
        }
    }
    return 1;
}

static sonic_error_t sonic_device_info_validate_bytes(const uint8_t *bytes,
                                                       size_t length)
{
    if (bytes == NULL) {
        return SONIC_INVALID_ARGUMENT;
    }
    if (length != SONIC_DEVICE_INFO_SIZE) {
        return SONIC_INVALID_DEVICE_INFO;
    }
    if (bytes[0] != 1u || bytes[1] != 1u ||
        (bytes[8] > 100u && bytes[8] != 0xFFu) ||
        bytes[10] != SONIC_MAX_MESSAGE_BYTES ||
        (bytes[11] & SONIC_RESERVED_CAPABILITY_MASK) != 0u) {
        return SONIC_INVALID_DEVICE_INFO;
    }
    return SONIC_OK;
}

static sonic_error_t sonic_frame_fields_validate(sonic_payload_type_t type,
                                                  uint16_t message_id,
                                                  uint8_t fragment_index,
                                                  uint8_t fragment_count,
                                                  size_t chunk_length)
{
    if (!sonic_type_is_known((uint8_t) type)) {
        return SONIC_UNKNOWN_TYPE;
    }
    if (message_id == 0u) {
        return SONIC_BAD_MESSAGE_ID;
    }
    if (fragment_count == 0u || fragment_count > SONIC_MAX_FRAGMENTS ||
        fragment_index >= fragment_count) {
        return SONIC_BAD_FRAGMENT;
    }
    if (chunk_length > SONIC_FRAME_PAYLOAD_SIZE) {
        return SONIC_BAD_LENGTH;
    }
    if (type == SONIC_TYPE_DEVICE_INFO &&
        (fragment_count != 1u || fragment_index != 0u ||
         chunk_length != SONIC_DEVICE_INFO_SIZE)) {
        return SONIC_INVALID_DEVICE_INFO;
    }
    return SONIC_OK;
}

uint16_t sonic_core_crc16(const uint8_t *bytes, size_t length)
{
    uint16_t crc = 0xFFFFu;

    if (bytes == NULL && length != 0u) {
        return 0u;
    }
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t) bytes[i] << 8;
        for (uint8_t bit = 0; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) != 0u
                      ? (uint16_t) ((crc << 1) ^ 0x1021u)
                      : (uint16_t) (crc << 1);
        }
    }
    return crc;
}

const char *sonic_error_name(sonic_error_t error)
{
    static const char *const names[] = {
        "OK", "BAD_MAGIC", "BAD_VERSION", "UNKNOWN_TYPE",
        "BAD_MESSAGE_ID", "BAD_FRAGMENT", "BAD_LENGTH", "BAD_PADDING",
        "BAD_CRC", "FRAGMENT_CONFLICT", "ASSEMBLY_TIMEOUT", "INVALID_UTF8",
        "UNSUPPORTED_GLYPH", "INVALID_URL", "AUDIO_INIT_FAILED",
        "AUDIO_READ_FAILED", "AUDIO_WRITE_FAILED", "WASM_INIT_FAILED",
        "BROWSER_AUDIO_SUSPENDED", "INVALID_ARGUMENT", "BUFFER_TOO_SMALL",
        "INVALID_DEVICE_INFO"
    };
    if ((size_t) error >= sizeof(names) / sizeof(names[0])) {
        return "UNKNOWN_ERROR";
    }
    return names[error];
}

sonic_error_t sonic_payload_validate(sonic_payload_type_t type,
                                     const uint8_t *payload,
                                     size_t payload_length)
{
    if ((payload == NULL && payload_length != 0u) ||
        payload_length > SONIC_MAX_MESSAGE_BYTES) {
        return payload_length > SONIC_MAX_MESSAGE_BYTES ? SONIC_BAD_LENGTH
                                                         : SONIC_INVALID_ARGUMENT;
    }

    switch (type) {
    case SONIC_TYPE_TEXT:
        return sonic_utf8_is_valid(payload, payload_length) ? SONIC_OK
                                                             : SONIC_INVALID_UTF8;
    case SONIC_TYPE_URL:
        if (!sonic_utf8_is_valid(payload, payload_length)) {
            return SONIC_INVALID_UTF8;
        }
        return sonic_url_has_supported_scheme(payload, payload_length)
                   ? SONIC_OK
                   : SONIC_INVALID_URL;
    case SONIC_TYPE_TOKEN:
        return SONIC_OK;
    case SONIC_TYPE_DEVICE_INFO:
        return sonic_device_info_validate_bytes(payload, payload_length);
    default:
        return SONIC_UNKNOWN_TYPE;
    }
}

sonic_error_t sonic_frame_encode(uint8_t *out_frame,
                                 size_t out_length,
                                 sonic_payload_type_t type,
                                 uint16_t message_id,
                                 uint8_t fragment_index,
                                 uint8_t fragment_count,
                                 const uint8_t *chunk,
                                 size_t chunk_length)
{
    sonic_error_t error;
    uint16_t crc;

    if (out_frame == NULL || (chunk == NULL && chunk_length != 0u)) {
        return SONIC_INVALID_ARGUMENT;
    }
    if (out_length != SONIC_FRAME_SIZE) {
        return SONIC_BAD_LENGTH;
    }
    error = sonic_frame_fields_validate(type, message_id, fragment_index,
                                        fragment_count, chunk_length);
    if (error != SONIC_OK) {
        return error;
    }
    if (type == SONIC_TYPE_DEVICE_INFO) {
        error = sonic_payload_validate(type, chunk, chunk_length);
        if (error != SONIC_OK) {
            return error;
        }
    }

    memset(out_frame, 0, SONIC_FRAME_SIZE);
    out_frame[0] = SONIC_MAGIC_0;
    out_frame[1] = SONIC_MAGIC_1;
    out_frame[2] = (uint8_t) ((SONIC_PROTOCOL_VERSION << SONIC_VERSION_SHIFT) |
                              ((uint8_t) type & SONIC_KNOWN_TYPE_MASK));
    out_frame[3] = (uint8_t) (message_id >> 8);
    out_frame[4] = (uint8_t) message_id;
    out_frame[5] = (uint8_t) ((fragment_index << 4) |
                              (uint8_t) (fragment_count - 1u));
    out_frame[6] = (uint8_t) chunk_length;
    if (chunk_length != 0u) {
        memcpy(out_frame + SONIC_FRAME_HEADER_SIZE, chunk, chunk_length);
    }
    crc = sonic_core_crc16(out_frame, SONIC_FRAME_CRC_OFFSET);
    out_frame[SONIC_FRAME_CRC_OFFSET] = (uint8_t) (crc >> 8);
    out_frame[SONIC_FRAME_CRC_OFFSET + 1u] = (uint8_t) crc;
    return SONIC_OK;
}

sonic_error_t sonic_frame_decode(const uint8_t *frame_bytes,
                                 size_t frame_length,
                                 sonic_frame_t *out_frame)
{
    uint8_t version;
    uint8_t type;
    uint16_t expected_crc;
    uint16_t actual_crc;
    uint8_t fragment_index;
    uint8_t fragment_count;
    size_t chunk_length;
    sonic_error_t error;

    if (frame_bytes == NULL || out_frame == NULL) {
        return SONIC_INVALID_ARGUMENT;
    }
    if (frame_length != SONIC_FRAME_SIZE) {
        return SONIC_BAD_LENGTH;
    }
    if (frame_bytes[0] != SONIC_MAGIC_0 || frame_bytes[1] != SONIC_MAGIC_1) {
        return SONIC_BAD_MAGIC;
    }
    version = frame_bytes[2] >> SONIC_VERSION_SHIFT;
    if (version != SONIC_PROTOCOL_VERSION) {
        return SONIC_BAD_VERSION;
    }
    type = frame_bytes[2] & SONIC_KNOWN_TYPE_MASK;
    if (!sonic_type_is_known(type)) {
        return SONIC_UNKNOWN_TYPE;
    }
    if (frame_bytes[3] == 0u && frame_bytes[4] == 0u) {
        return SONIC_BAD_MESSAGE_ID;
    }
    fragment_index = frame_bytes[5] >> 4;
    fragment_count = (uint8_t) ((frame_bytes[5] & 0x0Fu) + 1u);
    if (fragment_count > SONIC_MAX_FRAGMENTS ||
        fragment_index >= fragment_count) {
        return SONIC_BAD_FRAGMENT;
    }
    chunk_length = frame_bytes[6];
    if (chunk_length > SONIC_FRAME_PAYLOAD_SIZE) {
        return SONIC_BAD_LENGTH;
    }
    for (size_t i = SONIC_FRAME_HEADER_SIZE + chunk_length;
         i < SONIC_FRAME_CRC_OFFSET; ++i) {
        if (frame_bytes[i] != 0u) {
            return SONIC_BAD_PADDING;
        }
    }
    expected_crc = (uint16_t) (((uint16_t) frame_bytes[SONIC_FRAME_CRC_OFFSET]
                                << 8) |
                               frame_bytes[SONIC_FRAME_CRC_OFFSET + 1u]);
    actual_crc = sonic_core_crc16(frame_bytes, SONIC_FRAME_CRC_OFFSET);
    if (expected_crc != actual_crc) {
        return SONIC_BAD_CRC;
    }
    if (type == SONIC_TYPE_DEVICE_INFO &&
        (fragment_count != 1u || fragment_index != 0u ||
         chunk_length != SONIC_DEVICE_INFO_SIZE)) {
        return SONIC_INVALID_DEVICE_INFO;
    }

    memset(out_frame, 0, sizeof(*out_frame));
    out_frame->type = (sonic_payload_type_t) type;
    out_frame->message_id = (uint16_t) (((uint16_t) frame_bytes[3] << 8) |
                                        frame_bytes[4]);
    out_frame->fragment_index = fragment_index;
    out_frame->fragment_count = fragment_count;
    out_frame->chunk_length = (uint8_t) chunk_length;
    memcpy(out_frame->chunk, frame_bytes + SONIC_FRAME_HEADER_SIZE,
           chunk_length);
    if (fragment_count == 1u) {
        error = sonic_payload_validate(out_frame->type, out_frame->chunk,
                                       out_frame->chunk_length);
        if (error != SONIC_OK) {
            return error;
        }
    }
    return SONIC_OK;
}

uint16_t sonic_message_id_normalize(uint16_t seed)
{
    return seed == 0u ? 1u : seed;
}

uint16_t sonic_message_id_next(uint16_t current)
{
    current = (uint16_t) (current + 1u);
    return current == 0u ? 1u : current;
}

sonic_error_t sonic_message_fragment(sonic_payload_type_t type,
                                     uint16_t message_id,
                                     const uint8_t *payload,
                                     size_t payload_length,
                                     uint8_t (*out_frames)[SONIC_FRAME_SIZE],
                                     size_t frame_capacity,
                                     size_t *out_frame_count)
{
    size_t count;
    sonic_error_t error;

    if (out_frames == NULL || out_frame_count == NULL) {
        return SONIC_INVALID_ARGUMENT;
    }
    *out_frame_count = 0u;
    if (message_id == 0u) {
        return SONIC_BAD_MESSAGE_ID;
    }
    error = sonic_payload_validate(type, payload, payload_length);
    if (error != SONIC_OK) {
        return error;
    }
    count = payload_length == 0u
                ? 1u
                : (payload_length + SONIC_FRAME_PAYLOAD_SIZE - 1u) /
                      SONIC_FRAME_PAYLOAD_SIZE;
    if (count > frame_capacity) {
        return SONIC_BUFFER_TOO_SMALL;
    }

    for (size_t index = 0; index < count; ++index) {
        size_t offset = index * SONIC_FRAME_PAYLOAD_SIZE;
        size_t remaining = payload_length - offset;
        size_t chunk_length = remaining > SONIC_FRAME_PAYLOAD_SIZE
                                  ? SONIC_FRAME_PAYLOAD_SIZE
                                  : remaining;
        error = sonic_frame_encode(out_frames[index], SONIC_FRAME_SIZE, type,
                                   message_id, (uint8_t) index,
                                   (uint8_t) count,
                                   chunk_length == 0u ? NULL : payload + offset,
                                   chunk_length);
        if (error != SONIC_OK) {
            return error;
        }
    }
    *out_frame_count = count;
    return SONIC_OK;
}

sonic_error_t sonic_device_info_serialize(const sonic_device_info_t *info,
                                          uint8_t *out_bytes,
                                          size_t out_length)
{
    if (info == NULL || out_bytes == NULL) {
        return SONIC_INVALID_ARGUMENT;
    }
    if (out_length != SONIC_DEVICE_INFO_SIZE) {
        return SONIC_BAD_LENGTH;
    }
    out_bytes[0] = info->schema_version;
    out_bytes[1] = info->model_id;
    out_bytes[2] = info->sonic_major;
    out_bytes[3] = info->sonic_minor;
    out_bytes[4] = info->sonic_patch;
    out_bytes[5] = info->ggwave_major;
    out_bytes[6] = info->ggwave_minor;
    out_bytes[7] = info->ggwave_patch;
    out_bytes[8] = info->battery_percent;
    out_bytes[9] = info->acoustic_profile;
    out_bytes[10] = info->max_message_bytes;
    out_bytes[11] = info->capability_bits;
    memcpy(out_bytes + 12u, info->build_fingerprint,
           sizeof(info->build_fingerprint));
    return sonic_device_info_validate_bytes(out_bytes, SONIC_DEVICE_INFO_SIZE);
}

sonic_error_t sonic_device_info_parse(const uint8_t *bytes,
                                      size_t length,
                                      sonic_device_info_t *out_info)
{
    sonic_error_t error;

    if (bytes == NULL || out_info == NULL) {
        return SONIC_INVALID_ARGUMENT;
    }
    error = sonic_device_info_validate_bytes(bytes, length);
    if (error != SONIC_OK) {
        return error;
    }
    out_info->schema_version = bytes[0];
    out_info->model_id = bytes[1];
    out_info->sonic_major = bytes[2];
    out_info->sonic_minor = bytes[3];
    out_info->sonic_patch = bytes[4];
    out_info->ggwave_major = bytes[5];
    out_info->ggwave_minor = bytes[6];
    out_info->ggwave_patch = bytes[7];
    out_info->battery_percent = bytes[8];
    out_info->acoustic_profile = bytes[9];
    out_info->max_message_bytes = bytes[10];
    out_info->capability_bits = bytes[11];
    memcpy(out_info->build_fingerprint, bytes + 12u,
           sizeof(out_info->build_fingerprint));
    return SONIC_OK;
}

void sonic_reassembly_reset(sonic_reassembly_t *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

static void sonic_result_init(sonic_reassembly_result_t *result,
                              sonic_reassembly_status_t status,
                              sonic_error_t error)
{
    memset(result, 0, sizeof(*result));
    result->status = status;
    result->error = error;
}

static void sonic_result_from_state(const sonic_reassembly_t *state,
                                    sonic_reassembly_result_t *result,
                                    sonic_reassembly_status_t status,
                                    sonic_error_t error)
{
    size_t message_length = 0u;

    sonic_result_init(result, status, error);
    result->message_id = state->message_id;
    result->type = (sonic_payload_type_t) state->type;
    result->fragment_count = state->fragment_count;
    for (uint8_t index = 0; index < state->fragment_count; ++index) {
        if ((state->received_mask & (uint8_t) (1u << index)) != 0u) {
            result->received_fragments++;
            message_length += state->chunk_lengths[index];
        }
    }
    result->message_length = (uint8_t) message_length;
    if (status == SONIC_REASSEMBLY_COMPLETE) {
        size_t offset = 0u;
        for (uint8_t index = 0; index < state->fragment_count; ++index) {
            size_t chunk_length = state->chunk_lengths[index];
            memcpy(result->payload + offset, state->chunks[index], chunk_length);
            offset += chunk_length;
        }
    }
}

sonic_reassembly_status_t sonic_reassembly_poll(
    sonic_reassembly_t *state,
    uint32_t now_ms,
    sonic_reassembly_result_t *out_result)
{
    uint32_t elapsed;

    if (out_result == NULL) {
        return SONIC_REASSEMBLY_ERROR;
    }
    if (state == NULL || !state->active) {
        sonic_result_init(out_result, SONIC_REASSEMBLY_IDLE, SONIC_OK);
        return SONIC_REASSEMBLY_IDLE;
    }
    elapsed = now_ms - state->first_received_ms;
    if (elapsed >= SONIC_REASSEMBLY_EXPIRY_MS) {
        sonic_result_from_state(state, out_result, SONIC_REASSEMBLY_EXPIRED,
                                SONIC_OK);
        sonic_reassembly_reset(state);
        return SONIC_REASSEMBLY_EXPIRED;
    }
    if (elapsed >= SONIC_REASSEMBLY_TIMEOUT_MS && !state->timeout_reported) {
        state->timeout_reported = 1u;
        sonic_result_from_state(state, out_result, SONIC_REASSEMBLY_INCOMPLETE,
                                SONIC_ASSEMBLY_TIMEOUT);
        return SONIC_REASSEMBLY_INCOMPLETE;
    }
    sonic_result_from_state(state, out_result, SONIC_REASSEMBLY_IN_PROGRESS,
                            SONIC_OK);
    return SONIC_REASSEMBLY_IN_PROGRESS;
}

sonic_reassembly_status_t sonic_reassembly_accept(
    sonic_reassembly_t *state,
    const uint8_t *frame_bytes,
    size_t frame_length,
    uint32_t now_ms,
    sonic_reassembly_result_t *out_result)
{
    sonic_frame_t frame;
    sonic_error_t error;
    uint8_t bit;
    uint32_t elapsed;

    if (state == NULL || out_result == NULL) {
        return SONIC_REASSEMBLY_ERROR;
    }
    if (state->active &&
        (uint32_t) (now_ms - state->first_received_ms) >=
            SONIC_REASSEMBLY_EXPIRY_MS) {
        sonic_reassembly_reset(state);
    }
    error = sonic_frame_decode(frame_bytes, frame_length, &frame);
    if (error != SONIC_OK) {
        sonic_result_init(out_result, SONIC_REASSEMBLY_ERROR, error);
        return SONIC_REASSEMBLY_ERROR;
    }

    if (state->active && frame.message_id != state->message_id) {
        if (frame.fragment_index != 0u) {
            sonic_result_init(out_result, SONIC_REASSEMBLY_IGNORED, SONIC_OK);
            return SONIC_REASSEMBLY_IGNORED;
        }
        sonic_reassembly_reset(state);
    }
    if (!state->active) {
        if (frame.fragment_index != 0u) {
            sonic_result_init(out_result, SONIC_REASSEMBLY_IGNORED, SONIC_OK);
            return SONIC_REASSEMBLY_IGNORED;
        }
        state->active = 1u;
        state->type = (uint8_t) frame.type;
        state->fragment_count = frame.fragment_count;
        state->message_id = frame.message_id;
        state->first_received_ms = now_ms;
    } else if (state->type != (uint8_t) frame.type ||
               state->fragment_count != frame.fragment_count) {
        sonic_reassembly_reset(state);
        sonic_result_init(out_result, SONIC_REASSEMBLY_ERROR,
                          SONIC_FRAGMENT_CONFLICT);
        return SONIC_REASSEMBLY_ERROR;
    }

    bit = (uint8_t) (1u << frame.fragment_index);
    if ((state->received_mask & bit) != 0u) {
        if (state->chunk_lengths[frame.fragment_index] != frame.chunk_length ||
            memcmp(state->chunks[frame.fragment_index], frame.chunk,
                   frame.chunk_length) != 0) {
            sonic_reassembly_reset(state);
            sonic_result_init(out_result, SONIC_REASSEMBLY_ERROR,
                              SONIC_FRAGMENT_CONFLICT);
            return SONIC_REASSEMBLY_ERROR;
        }
        sonic_result_from_state(state, out_result, SONIC_REASSEMBLY_DUPLICATE,
                                SONIC_OK);
        return SONIC_REASSEMBLY_DUPLICATE;
    }

    state->chunk_lengths[frame.fragment_index] = frame.chunk_length;
    memcpy(state->chunks[frame.fragment_index], frame.chunk, frame.chunk_length);
    state->received_mask |= bit;
    if (state->received_mask != (uint8_t) ((1u << state->fragment_count) - 1u)) {
        sonic_result_from_state(state, out_result, SONIC_REASSEMBLY_IN_PROGRESS,
                                SONIC_OK);
        return SONIC_REASSEMBLY_IN_PROGRESS;
    }

    {
        size_t total_length = 0u;
        uint8_t assembled[SONIC_MAX_MESSAGE_BYTES];
        for (uint8_t index = 0; index < state->fragment_count; ++index) {
            if (total_length + state->chunk_lengths[index] >
                SONIC_MAX_MESSAGE_BYTES) {
                sonic_reassembly_reset(state);
                sonic_result_init(out_result, SONIC_REASSEMBLY_ERROR,
                                  SONIC_BAD_LENGTH);
                return SONIC_REASSEMBLY_ERROR;
            }
            memcpy(assembled + total_length, state->chunks[index],
                   state->chunk_lengths[index]);
            total_length += state->chunk_lengths[index];
        }
        error = sonic_payload_validate((sonic_payload_type_t) state->type,
                                       assembled, total_length);
        if (error != SONIC_OK) {
            sonic_reassembly_reset(state);
            sonic_result_init(out_result, SONIC_REASSEMBLY_ERROR, error);
            return SONIC_REASSEMBLY_ERROR;
        }
        sonic_result_from_state(state, out_result, SONIC_REASSEMBLY_COMPLETE,
                                SONIC_OK);
    }
    elapsed = now_ms - state->first_received_ms;
    (void) elapsed;
    sonic_reassembly_reset(state);
    return SONIC_REASSEMBLY_COMPLETE;
}
