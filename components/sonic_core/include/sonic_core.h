#ifndef SONIC_CORE_H
#define SONIC_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SONIC_PROTOCOL_VERSION 1u
#define SONIC_FRAME_SIZE 40u
#define SONIC_FRAME_HEADER_SIZE 7u
#define SONIC_FRAME_PAYLOAD_SIZE 31u
#define SONIC_FRAME_CRC_OFFSET 38u
#define SONIC_MAX_FRAGMENTS 3u
#define SONIC_MAX_MESSAGE_BYTES 93u
#define SONIC_DEVICE_INFO_SIZE 16u
#define SONIC_REASSEMBLY_TIMEOUT_MS 10000u
#define SONIC_REASSEMBLY_EXPIRY_MS 30000u

typedef enum {
    SONIC_TYPE_TEXT = 1,
    SONIC_TYPE_URL = 2,
    SONIC_TYPE_TOKEN = 3,
    SONIC_TYPE_DEVICE_INFO = 4
} sonic_payload_type_t;

typedef enum {
    SONIC_OK = 0,
    SONIC_BAD_MAGIC,
    SONIC_BAD_VERSION,
    SONIC_UNKNOWN_TYPE,
    SONIC_BAD_MESSAGE_ID,
    SONIC_BAD_FRAGMENT,
    SONIC_BAD_LENGTH,
    SONIC_BAD_PADDING,
    SONIC_BAD_CRC,
    SONIC_FRAGMENT_CONFLICT,
    SONIC_ASSEMBLY_TIMEOUT,
    SONIC_INVALID_UTF8,
    SONIC_UNSUPPORTED_GLYPH,
    SONIC_INVALID_URL,
    SONIC_AUDIO_INIT_FAILED,
    SONIC_AUDIO_READ_FAILED,
    SONIC_AUDIO_WRITE_FAILED,
    SONIC_WASM_INIT_FAILED,
    SONIC_BROWSER_AUDIO_SUSPENDED,
    SONIC_INVALID_ARGUMENT,
    SONIC_BUFFER_TOO_SMALL,
    SONIC_INVALID_DEVICE_INFO
} sonic_error_t;

typedef struct {
    sonic_payload_type_t type;
    uint16_t message_id;
    uint8_t fragment_index;
    uint8_t fragment_count;
    uint8_t chunk_length;
    uint8_t chunk[SONIC_FRAME_PAYLOAD_SIZE];
} sonic_frame_t;

typedef struct {
    uint8_t schema_version;
    uint8_t model_id;
    uint8_t sonic_major;
    uint8_t sonic_minor;
    uint8_t sonic_patch;
    uint8_t ggwave_major;
    uint8_t ggwave_minor;
    uint8_t ggwave_patch;
    uint8_t battery_percent;
    uint8_t acoustic_profile;
    uint8_t max_message_bytes;
    uint8_t capability_bits;
    uint8_t build_fingerprint[4];
} sonic_device_info_t;

typedef enum {
    SONIC_REASSEMBLY_IDLE = 0,
    SONIC_REASSEMBLY_IN_PROGRESS,
    SONIC_REASSEMBLY_DUPLICATE,
    SONIC_REASSEMBLY_IGNORED,
    SONIC_REASSEMBLY_INCOMPLETE,
    SONIC_REASSEMBLY_COMPLETE,
    SONIC_REASSEMBLY_ERROR,
    SONIC_REASSEMBLY_EXPIRED
} sonic_reassembly_status_t;

typedef struct {
    sonic_reassembly_status_t status;
    sonic_error_t error;
    uint16_t message_id;
    sonic_payload_type_t type;
    uint8_t received_fragments;
    uint8_t fragment_count;
    uint8_t message_length;
    uint8_t payload[SONIC_MAX_MESSAGE_BYTES];
} sonic_reassembly_result_t;

typedef struct {
    uint8_t active;
    uint8_t type;
    uint8_t fragment_count;
    uint8_t received_mask;
    uint8_t chunk_lengths[SONIC_MAX_FRAGMENTS];
    uint8_t chunks[SONIC_MAX_FRAGMENTS][SONIC_FRAME_PAYLOAD_SIZE];
    uint16_t message_id;
    uint32_t first_received_ms;
    uint8_t timeout_reported;
} sonic_reassembly_t;

uint16_t sonic_core_crc16(const uint8_t *bytes, size_t length);
const char *sonic_error_name(sonic_error_t error);

sonic_error_t sonic_payload_validate(sonic_payload_type_t type,
                                     const uint8_t *payload,
                                     size_t payload_length);
sonic_error_t sonic_frame_encode(uint8_t *out_frame,
                                 size_t out_length,
                                 sonic_payload_type_t type,
                                 uint16_t message_id,
                                 uint8_t fragment_index,
                                 uint8_t fragment_count,
                                 const uint8_t *chunk,
                                 size_t chunk_length);
sonic_error_t sonic_frame_decode(const uint8_t *frame_bytes,
                                 size_t frame_length,
                                 sonic_frame_t *out_frame);

uint16_t sonic_message_id_normalize(uint16_t seed);
uint16_t sonic_message_id_next(uint16_t current);
sonic_error_t sonic_message_fragment(sonic_payload_type_t type,
                                     uint16_t message_id,
                                     const uint8_t *payload,
                                     size_t payload_length,
                                     uint8_t (*out_frames)[SONIC_FRAME_SIZE],
                                     size_t frame_capacity,
                                     size_t *out_frame_count);

sonic_error_t sonic_device_info_serialize(const sonic_device_info_t *info,
                                          uint8_t *out_bytes,
                                          size_t out_length);
sonic_error_t sonic_device_info_parse(const uint8_t *bytes,
                                      size_t length,
                                      sonic_device_info_t *out_info);

void sonic_reassembly_reset(sonic_reassembly_t *state);
sonic_reassembly_status_t sonic_reassembly_poll(
    sonic_reassembly_t *state,
    uint32_t now_ms,
    sonic_reassembly_result_t *out_result);
sonic_reassembly_status_t sonic_reassembly_accept(
    sonic_reassembly_t *state,
    const uint8_t *frame_bytes,
    size_t frame_length,
    uint32_t now_ms,
    sonic_reassembly_result_t *out_result);

#ifdef __cplusplus
}
#endif

#endif
