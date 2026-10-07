#include "sonic_memory_gate.h"

#include <stdint.h>
#include <string.h>

void sonic_memory_evaluate(const sonic_memory_measurement_t *measurement,
                           sonic_memory_result_t *result)
{
    if (result == NULL) {
        return;
    }
    memset(result, 0, sizeof(*result));
    if (measurement == NULL) {
        return;
    }

    const bool reserve_addition_fits =
        measurement->codec_required_heap_bytes <= SIZE_MAX - SONIC_MEMORY_RESERVE_BYTES;
    if (reserve_addition_fits) {
        result->codec_required_with_reserve_bytes =
            measurement->codec_required_heap_bytes + SONIC_MEMORY_RESERVE_BYTES;
    }
    result->pre_codec_passed = measurement->pre_codec_measured &&
        measurement->codec_required_heap_bytes > 0u && reserve_addition_fits &&
        measurement->pre_codec_largest_free_block_bytes >=
            result->codec_required_with_reserve_bytes;
    result->runtime_free_passed =
        measurement->runtime_free_heap_bytes >= SONIC_MEMORY_RUNTIME_FREE_MIN_BYTES;
    result->runtime_minimum_free_passed =
        measurement->runtime_minimum_free_heap_bytes >=
            SONIC_MEMORY_RUNTIME_MIN_FREE_MIN_BYTES;
    result->runtime_largest_block_passed =
        measurement->runtime_largest_free_block_bytes >=
            SONIC_MEMORY_RUNTIME_LARGEST_BLOCK_MIN_BYTES;
    result->thresholds_passed = result->pre_codec_passed &&
        result->runtime_free_passed && result->runtime_minimum_free_passed &&
        result->runtime_largest_block_passed;
}
