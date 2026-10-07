#include "sonic_core.h"
#include "sonic_golden_vectors.h"

#include <stdio.h>
#include <string.h>

static void print_hex(const uint8_t *bytes, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        printf("%02X", bytes[i]);
    }
}

static void set_crc(uint8_t frame[SONIC_FRAME_SIZE])
{
    const uint16_t crc = sonic_core_crc16(frame, SONIC_FRAME_CRC_OFFSET);
    frame[SONIC_FRAME_CRC_OFFSET] = (uint8_t) (crc >> 8);
    frame[SONIC_FRAME_CRC_OFFSET + 1u] = (uint8_t) crc;
}

static int emit_error(const char *name, sonic_error_t error, sonic_error_t expected)
{
    printf("error:%s:%u:%s\n", name, (unsigned) error, sonic_error_name(error));
    return error != expected;
}

static int emit_malformed_cases(const sonic_golden_vector_t *text_a)
{
    uint8_t frame[SONIC_FRAME_SIZE];
    sonic_frame_t decoded;
    int failed = 0;

    memcpy(frame, text_a->frames[0], sizeof(frame));
    failed |= emit_error("bad_length", sonic_frame_decode(frame, SONIC_FRAME_SIZE - 1u, &decoded), SONIC_BAD_LENGTH);

    memcpy(frame, text_a->frames[0], sizeof(frame));
    frame[0] ^= 0x01u;
    set_crc(frame);
    failed |= emit_error("bad_magic", sonic_frame_decode(frame, sizeof(frame), &decoded), SONIC_BAD_MAGIC);

    memcpy(frame, text_a->frames[0], sizeof(frame));
    frame[2] = (uint8_t) ((2u << 4) | SONIC_TYPE_TEXT);
    set_crc(frame);
    failed |= emit_error("bad_version", sonic_frame_decode(frame, sizeof(frame), &decoded), SONIC_BAD_VERSION);

    memcpy(frame, text_a->frames[0], sizeof(frame));
    frame[2] = (uint8_t) ((SONIC_PROTOCOL_VERSION << 4) | 0x0Fu);
    set_crc(frame);
    failed |= emit_error("unknown_type", sonic_frame_decode(frame, sizeof(frame), &decoded), SONIC_UNKNOWN_TYPE);

    memcpy(frame, text_a->frames[0], sizeof(frame));
    frame[3] = 0u;
    frame[4] = 0u;
    set_crc(frame);
    failed |= emit_error("zero_message_id", sonic_frame_decode(frame, sizeof(frame), &decoded), SONIC_BAD_MESSAGE_ID);

    memcpy(frame, text_a->frames[0], sizeof(frame));
    frame[5] = 0x03u;
    set_crc(frame);
    failed |= emit_error("bad_fragment", sonic_frame_decode(frame, sizeof(frame), &decoded), SONIC_BAD_FRAGMENT);

    memcpy(frame, text_a->frames[0], sizeof(frame));
    frame[6] = SONIC_FRAME_PAYLOAD_SIZE + 1u;
    set_crc(frame);
    failed |= emit_error("bad_chunk_length", sonic_frame_decode(frame, sizeof(frame), &decoded), SONIC_BAD_LENGTH);

    memcpy(frame, text_a->frames[0], sizeof(frame));
    frame[8] = 0xA5u;
    set_crc(frame);
    failed |= emit_error("bad_padding", sonic_frame_decode(frame, sizeof(frame), &decoded), SONIC_BAD_PADDING);

    memcpy(frame, text_a->frames[0], sizeof(frame));
    frame[SONIC_FRAME_CRC_OFFSET + 1u] ^= 0x01u;
    failed |= emit_error("bad_crc", sonic_frame_decode(frame, sizeof(frame), &decoded), SONIC_BAD_CRC);

    {
        static const uint8_t invalid_utf8[] = {0xC0u, 0xAFu};
        static const uint8_t invalid_url[] = {'f', 't', 'p', ':', '/', '/', 'x'};
        uint8_t invalid_device[SONIC_DEVICE_INFO_SIZE] = {
            2u, 1u, 1u, 2u, 3u, 4u, 5u, 6u, 100u, 0u,
            SONIC_MAX_MESSAGE_BYTES, 0u, 0xDEu, 0xADu, 0xBEu, 0xEFu
        };
        sonic_device_info_t parsed;
        failed |= emit_error("invalid_utf8", sonic_payload_validate(SONIC_TYPE_TEXT,
                                                                       invalid_utf8,
                                                                       sizeof(invalid_utf8)), SONIC_INVALID_UTF8);
        failed |= emit_error("invalid_url", sonic_payload_validate(SONIC_TYPE_URL,
                                                                     invalid_url,
                                                                     sizeof(invalid_url)), SONIC_INVALID_URL);
        failed |= emit_error("invalid_device_info", sonic_device_info_parse(invalid_device,
                                                                                sizeof(invalid_device),
                                                                                &parsed), SONIC_INVALID_DEVICE_INFO);
    }
    return failed;
}

static void emit_reassembly_scenario(const char *name,
                                     const sonic_golden_vector_t *vector)
{
    sonic_reassembly_t state;
    sonic_reassembly_result_t result;
    sonic_reassembly_reset(&state);
    printf("reassembly:%s", name);
    for (size_t i = 0; i < vector->frame_count; ++i) {
        const sonic_reassembly_status_t status = sonic_reassembly_accept(
            &state, vector->frames[i], SONIC_FRAME_SIZE, (uint32_t) (100u + i * 100u), &result);
        printf(":%u/%u/%u/%u/%u", (unsigned) status, (unsigned) result.error,
               (unsigned) result.received_fragments, (unsigned) result.fragment_count,
               (unsigned) result.message_length);
    }
    printf(":%u/%u/", (unsigned) result.message_id, (unsigned) result.type);
    print_hex(result.payload, result.message_length);
    putchar('\n');
}

static int emit_state_scenarios(const sonic_golden_vector_t *multi,
                                const sonic_golden_vector_t *triple,
                                const sonic_golden_vector_t *single)
{
    sonic_reassembly_t state;
    sonic_reassembly_result_t result;
    sonic_reassembly_status_t status;
    uint8_t conflict[SONIC_FRAME_SIZE];
    int failed = 0;

    sonic_reassembly_reset(&state);
    status = sonic_reassembly_accept(&state, multi->frames[0], SONIC_FRAME_SIZE, 100u, &result);
    printf("state:first:%u/%u/%u\n", (unsigned) status, (unsigned) result.error,
           (unsigned) result.received_fragments);
    status = sonic_reassembly_accept(&state, multi->frames[0], SONIC_FRAME_SIZE, 200u, &result);
    printf("state:duplicate:%u/%u/%u\n", (unsigned) status, (unsigned) result.error,
           (unsigned) result.received_fragments);
    memcpy(conflict, multi->frames[0], sizeof(conflict));
    conflict[7] ^= 1u;
    set_crc(conflict);
    status = sonic_reassembly_accept(&state, conflict, sizeof(conflict), 300u, &result);
    printf("state:conflict:%u/%u/%u\n", (unsigned) status, (unsigned) result.error,
           (unsigned) state.active);
    failed |= status != SONIC_REASSEMBLY_ERROR || result.error != SONIC_FRAGMENT_CONFLICT;

    sonic_reassembly_reset(&state);
    status = sonic_reassembly_accept(&state, multi->frames[1], SONIC_FRAME_SIZE, 400u, &result);
    printf("state:unrelated_fragment:%u/%u/%u\n", (unsigned) status, (unsigned) result.error,
           (unsigned) state.active);
    failed |= status != SONIC_REASSEMBLY_IGNORED;

    sonic_reassembly_reset(&state);
    status = sonic_reassembly_accept(&state, triple->frames[2], SONIC_FRAME_SIZE, 450u, &result);
    printf("state:unrelated_fragment2:%u/%u/%u\n", (unsigned) status,
           (unsigned) result.error, (unsigned) state.active);
    failed |= status != SONIC_REASSEMBLY_IGNORED;

    status = sonic_reassembly_accept(&state, multi->frames[0], SONIC_FRAME_SIZE, 500u, &result);
    printf("state:begin:%u/%u/%u\n", (unsigned) status, (unsigned) result.error,
           (unsigned) result.received_fragments);

    sonic_reassembly_reset(&state);
    (void) sonic_reassembly_accept(&state, multi->frames[0], SONIC_FRAME_SIZE, 1000u, &result);
    status = sonic_reassembly_poll(&state, 10999u, &result);
    printf("state:before_timeout:%u/%u/%u\n", (unsigned) status, (unsigned) result.error,
           (unsigned) state.active);
    status = sonic_reassembly_poll(&state, 11000u, &result);
    printf("state:timeout:%u/%u/%u\n", (unsigned) status, (unsigned) result.error,
           (unsigned) state.active);
    failed |= status != SONIC_REASSEMBLY_INCOMPLETE || result.error != SONIC_ASSEMBLY_TIMEOUT;
    status = sonic_reassembly_accept(&state, multi->frames[1], SONIC_FRAME_SIZE, 12000u, &result);
    printf("state:retry_complete:%u/%u/%u/%u/", (unsigned) status, (unsigned) result.error,
           (unsigned) result.message_length, (unsigned) result.received_fragments);
    print_hex(result.payload, result.message_length);
    putchar('\n');
    failed |= status != SONIC_REASSEMBLY_COMPLETE;

    sonic_reassembly_reset(&state);
    (void) sonic_reassembly_accept(&state, multi->frames[0], SONIC_FRAME_SIZE, 2000u, &result);
    status = sonic_reassembly_poll(&state, 32000u, &result);
    printf("state:expiry:%u/%u/%u\n", (unsigned) status, (unsigned) result.error,
           (unsigned) state.active);
    failed |= status != SONIC_REASSEMBLY_EXPIRED || state.active != 0u;

    sonic_reassembly_reset(&state);
    (void) sonic_reassembly_accept(&state, multi->frames[0], SONIC_FRAME_SIZE, 4000u, &result);
    status = sonic_reassembly_accept(&state, single->frames[0], SONIC_FRAME_SIZE, 4100u, &result);
    printf("state:new_message_fragment0:%u/%u/%u/%u/", (unsigned) status,
           (unsigned) result.message_id, (unsigned) result.type, (unsigned) result.message_length);
    print_hex(result.payload, result.message_length);
    putchar('\n');
    failed |= status != SONIC_REASSEMBLY_COMPLETE || result.message_id != single->message_id;

    emit_reassembly_scenario("three_frame", triple);
    return failed;
}

static int emit_device_info(const sonic_golden_vector_t *vector)
{
    sonic_device_info_t parsed;
    uint8_t serialized[SONIC_DEVICE_INFO_SIZE];
    int failed = 0;
    if (sonic_device_info_parse(vector->payload, vector->payload_length, &parsed) != SONIC_OK ||
        sonic_payload_validate(SONIC_TYPE_DEVICE_INFO, vector->payload, vector->payload_length) != SONIC_OK) {
        return 1;
    }
    printf("device_info:payload:");
    print_hex(vector->payload, vector->payload_length);
    printf("\ndevice_info:fields:%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/",
           (unsigned) parsed.schema_version, (unsigned) parsed.model_id,
           (unsigned) parsed.sonic_major, (unsigned) parsed.sonic_minor,
           (unsigned) parsed.sonic_patch, (unsigned) parsed.ggwave_major,
           (unsigned) parsed.ggwave_minor, (unsigned) parsed.ggwave_patch,
           (unsigned) parsed.battery_percent, (unsigned) parsed.acoustic_profile,
           (unsigned) parsed.max_message_bytes, (unsigned) parsed.capability_bits);
    print_hex(parsed.build_fingerprint, sizeof(parsed.build_fingerprint));
    putchar('\n');
    if (sonic_device_info_serialize(&parsed, serialized, sizeof(serialized)) != SONIC_OK ||
        memcmp(serialized, vector->payload, sizeof(serialized)) != 0) {
        return 1;
    }
    printf("device_info:serialized:");
    print_hex(serialized, sizeof(serialized));
    putchar('\n');

    {
        static const struct {
            const char *name;
            size_t offset;
            uint8_t value;
        } mutations[] = {
            {"schema", 0u, 2u}, {"reserved_model", 1u, 2u},
            {"battery_overflow", 8u, 101u}, {"max_message", 10u, SONIC_MAX_MESSAGE_BYTES - 1u},
            {"reserved_capabilities", 11u, 0x80u}
        };
        for (size_t i = 0; i < sizeof(mutations) / sizeof(mutations[0]); ++i) {
            uint8_t invalid[SONIC_DEVICE_INFO_SIZE];
            memcpy(invalid, vector->payload, sizeof(invalid));
            invalid[mutations[i].offset] = mutations[i].value;
            if (i == 1u) {
                failed |= emit_error(mutations[i].name,
                                     sonic_device_info_parse(invalid, sizeof(invalid), &parsed),
                                     SONIC_INVALID_DEVICE_INFO);
            } else {
                failed |= emit_error(mutations[i].name,
                                     sonic_payload_validate(SONIC_TYPE_DEVICE_INFO, invalid, sizeof(invalid)),
                                     SONIC_INVALID_DEVICE_INFO);
            }
        }
        {
            uint8_t sentinel_battery[SONIC_DEVICE_INFO_SIZE];
            memcpy(sentinel_battery, vector->payload, sizeof(sentinel_battery));
            sentinel_battery[8] = 0xFFu;
            failed |= emit_error("battery_unknown_sentinel",
                                 sonic_payload_validate(SONIC_TYPE_DEVICE_INFO, sentinel_battery,
                                                        sizeof(sentinel_battery)), SONIC_OK);
        }
        failed |= emit_error("device_info_short_parse",
                             sonic_device_info_parse(vector->payload,
                                                     vector->payload_length - 1u, &parsed),
                             SONIC_INVALID_DEVICE_INFO);
        failed |= emit_error("device_info_short_serialize",
                             sonic_device_info_serialize(&parsed, serialized,
                                                         sizeof(serialized) - 1u), SONIC_BAD_LENGTH);
    }
    return failed;
}

int main(void)
{
    const sonic_golden_vector_t *text_a = NULL;
    const sonic_golden_vector_t *multi = NULL;
    const sonic_golden_vector_t *triple = NULL;
    const sonic_golden_vector_t *device_info = NULL;
    int failed = 0;

    printf("parity:protocol_version:%u\n", SONIC_PROTOCOL_VERSION);
    for (size_t i = 0; i < sonic_golden_vector_count; ++i) {
        const sonic_golden_vector_t *vector = &sonic_golden_vectors[i];
        uint8_t frames[SONIC_MAX_FRAGMENTS][SONIC_FRAME_SIZE];
        size_t frame_count = 0u;
        const sonic_error_t fragment_error = sonic_message_fragment(
            vector->type, vector->message_id, vector->payload, vector->payload_length,
            frames, SONIC_MAX_FRAGMENTS, &frame_count);
        printf("vector:%s:%u:%u:%u:%zu\n", vector->name, (unsigned) vector->type,
               (unsigned) vector->message_id, (unsigned) fragment_error, frame_count);
        failed |= fragment_error != SONIC_OK || frame_count != vector->frame_count;

        for (size_t frame_index = 0; frame_index < frame_count; ++frame_index) {
            sonic_frame_t decoded;
            const sonic_error_t decode_error = sonic_frame_decode(frames[frame_index],
                                                                   SONIC_FRAME_SIZE,
                                                                   &decoded);
            printf("frame:%s:%zu:", vector->name, frame_index);
            print_hex(frames[frame_index], SONIC_FRAME_SIZE);
            printf("\nparse:%s:%zu:%u:", vector->name, frame_index, (unsigned) decode_error);
            if (decode_error == SONIC_OK) {
                printf("%u/%u/%u/%u/%u/", (unsigned) decoded.type,
                       (unsigned) decoded.message_id, (unsigned) decoded.fragment_index,
                       (unsigned) decoded.fragment_count, (unsigned) decoded.chunk_length);
                print_hex(decoded.chunk, decoded.chunk_length);
            }
            putchar('\n');
            failed |= decode_error != SONIC_OK ||
                      memcmp(frames[frame_index], vector->frames[frame_index], SONIC_FRAME_SIZE) != 0;
        }

        emit_reassembly_scenario(vector->name, vector);
        if (strcmp(vector->name, "text_a") == 0) text_a = vector;
        if (strcmp(vector->name, "multi_text") == 0) multi = vector;
        if (strcmp(vector->name, "max_ascii_text") == 0) triple = vector;
        if (strcmp(vector->name, "device_info") == 0) device_info = vector;
    }

    if (text_a == NULL || multi == NULL || triple == NULL || device_info == NULL) {
        return 2;
    }
    failed |= emit_malformed_cases(text_a);
    failed |= emit_state_scenarios(multi, triple, text_a);
    failed |= emit_device_info(device_info);
    printf("parity:result:%s\n", failed == 0 ? "PASS" : "FAIL");
    return failed == 0 ? 0 : 1;
}
