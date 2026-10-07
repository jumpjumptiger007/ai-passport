#ifndef SONIC_GOLDEN_VECTORS_H
#define SONIC_GOLDEN_VECTORS_H

#include "sonic_core.h"

typedef struct {
    const char *name;
    sonic_payload_type_t type;
    uint16_t message_id;
    const uint8_t *payload;
    size_t payload_length;
    size_t frame_count;
    const uint8_t (*frames)[SONIC_FRAME_SIZE];
} sonic_golden_vector_t;

extern const sonic_golden_vector_t sonic_golden_vectors[];
extern const size_t sonic_golden_vector_count;

#endif
