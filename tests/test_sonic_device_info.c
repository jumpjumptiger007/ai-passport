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

int main(void)
{
    const sonic_golden_vector_t *vector = find_vector("device_info");
    sonic_device_info_t parsed;
    sonic_device_info_t original = {
        .schema_version = 1u,
        .model_id = 1u,
        .sonic_major = 1u,
        .sonic_minor = 2u,
        .sonic_patch = 3u,
        .ggwave_major = 4u,
        .ggwave_minor = 5u,
        .ggwave_patch = 6u,
        .battery_percent = 100u,
        .acoustic_profile = 0u,
        .max_message_bytes = SONIC_MAX_MESSAGE_BYTES,
        .capability_bits = 0x7Fu,
        .build_fingerprint = {0xDEu, 0xADu, 0xBEu, 0xEFu}
    };
    uint8_t serialized[SONIC_DEVICE_INFO_SIZE];
    uint8_t invalid[SONIC_DEVICE_INFO_SIZE];

    assert(vector != NULL && vector->payload_length == SONIC_DEVICE_INFO_SIZE);
    assert(sonic_device_info_serialize(&original, serialized,
                                       sizeof(serialized)) == SONIC_OK);
    assert(memcmp(serialized, vector->payload, sizeof(serialized)) == 0);
    assert(sonic_device_info_parse(vector->payload, vector->payload_length,
                                   &parsed) == SONIC_OK);
    assert(parsed.schema_version == original.schema_version);
    assert(parsed.model_id == original.model_id);
    assert(parsed.sonic_major == original.sonic_major);
    assert(parsed.sonic_minor == original.sonic_minor);
    assert(parsed.sonic_patch == original.sonic_patch);
    assert(parsed.ggwave_major == original.ggwave_major);
    assert(parsed.ggwave_minor == original.ggwave_minor);
    assert(parsed.ggwave_patch == original.ggwave_patch);
    assert(parsed.battery_percent == original.battery_percent);
    assert(parsed.acoustic_profile == original.acoustic_profile);
    assert(parsed.max_message_bytes == original.max_message_bytes);
    assert(parsed.capability_bits == original.capability_bits);
    assert(memcmp(parsed.build_fingerprint, original.build_fingerprint, 4u) == 0);
    assert(sonic_payload_validate(SONIC_TYPE_DEVICE_INFO, serialized,
                                  sizeof(serialized)) == SONIC_OK);

    memcpy(invalid, serialized, sizeof(invalid));
    invalid[0] = 2u;
    assert(sonic_payload_validate(SONIC_TYPE_DEVICE_INFO, invalid,
                                  sizeof(invalid)) == SONIC_INVALID_DEVICE_INFO);
    memcpy(invalid, serialized, sizeof(invalid));
    invalid[1] = 2u;
    assert(sonic_device_info_parse(invalid, sizeof(invalid), &parsed) ==
           SONIC_INVALID_DEVICE_INFO);
    memcpy(invalid, serialized, sizeof(invalid));
    invalid[8] = 101u;
    assert(sonic_payload_validate(SONIC_TYPE_DEVICE_INFO, invalid,
                                  sizeof(invalid)) == SONIC_INVALID_DEVICE_INFO);
    memcpy(invalid, serialized, sizeof(invalid));
    invalid[8] = 0xFFu;
    assert(sonic_payload_validate(SONIC_TYPE_DEVICE_INFO, invalid,
                                  sizeof(invalid)) == SONIC_OK);
    memcpy(invalid, serialized, sizeof(invalid));
    invalid[10] = 92u;
    assert(sonic_payload_validate(SONIC_TYPE_DEVICE_INFO, invalid,
                                  sizeof(invalid)) == SONIC_INVALID_DEVICE_INFO);
    memcpy(invalid, serialized, sizeof(invalid));
    invalid[11] = 0x80u;
    assert(sonic_payload_validate(SONIC_TYPE_DEVICE_INFO, invalid,
                                  sizeof(invalid)) == SONIC_INVALID_DEVICE_INFO);
    assert(sonic_device_info_parse(serialized, sizeof(serialized) - 1u,
                                  &parsed) == SONIC_INVALID_DEVICE_INFO);
    assert(sonic_device_info_serialize(&original, serialized,
                                       sizeof(serialized) - 1u) == SONIC_BAD_LENGTH);

    return 0;
}
