#ifndef SONIC_MEMORY_GATE_H
#define SONIC_MEMORY_GATE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SONIC_MEMORY_RESERVE_BYTES (24u * 1024u)
#define SONIC_MEMORY_RUNTIME_FREE_MIN_BYTES (48u * 1024u)
#define SONIC_MEMORY_RUNTIME_MIN_FREE_MIN_BYTES (32u * 1024u)
#define SONIC_MEMORY_RUNTIME_LARGEST_BLOCK_MIN_BYTES (24u * 1024u)

typedef struct {
    bool pre_codec_measured;
    size_t codec_required_heap_bytes;
    size_t pre_codec_largest_free_block_bytes;
    size_t runtime_free_heap_bytes;
    size_t runtime_minimum_free_heap_bytes;
    size_t runtime_largest_free_block_bytes;
} sonic_memory_measurement_t;

typedef struct {
    size_t codec_required_with_reserve_bytes;
    bool pre_codec_passed;
    bool runtime_free_passed;
    bool runtime_minimum_free_passed;
    bool runtime_largest_block_passed;
    bool thresholds_passed;
} sonic_memory_result_t;

void sonic_memory_evaluate(const sonic_memory_measurement_t *measurement,
                           sonic_memory_result_t *result);

#ifdef __cplusplus
}
#endif

#endif
