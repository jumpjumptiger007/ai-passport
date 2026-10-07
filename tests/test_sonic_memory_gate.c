#include "sonic_memory_gate.h"

#include <assert.h>
#include <stdint.h>

static sonic_memory_measurement_t passing_measurement(void)
{
    sonic_memory_measurement_t measurement = {0};
    measurement.pre_codec_measured = true;
    measurement.codec_required_heap_bytes = 99824u;
    measurement.pre_codec_largest_free_block_bytes = 124400u;
    measurement.runtime_free_heap_bytes = 49152u;
    measurement.runtime_minimum_free_heap_bytes = 32768u;
    measurement.runtime_largest_free_block_bytes = 24576u;
    return measurement;
}

int main(void)
{
    sonic_memory_result_t result;
    sonic_memory_measurement_t measurement = passing_measurement();

    sonic_memory_evaluate(&measurement, &result);
    assert(result.codec_required_with_reserve_bytes == 124400u);
    assert(result.pre_codec_passed);
    assert(result.runtime_free_passed);
    assert(result.runtime_minimum_free_passed);
    assert(result.runtime_largest_block_passed);
    assert(result.thresholds_passed);

    measurement.pre_codec_largest_free_block_bytes = 124399u;
    sonic_memory_evaluate(&measurement, &result);
    assert(!result.pre_codec_passed && !result.thresholds_passed);

    measurement = passing_measurement();
    measurement.pre_codec_measured = false;
    sonic_memory_evaluate(&measurement, &result);
    assert(!result.pre_codec_passed && !result.thresholds_passed);

    measurement = passing_measurement();
    measurement.runtime_free_heap_bytes = 49151u;
    sonic_memory_evaluate(&measurement, &result);
    assert(!result.runtime_free_passed && !result.thresholds_passed);

    measurement = passing_measurement();
    measurement.runtime_minimum_free_heap_bytes = 32767u;
    sonic_memory_evaluate(&measurement, &result);
    assert(!result.runtime_minimum_free_passed && !result.thresholds_passed);

    measurement = passing_measurement();
    measurement.runtime_largest_free_block_bytes = 24575u;
    sonic_memory_evaluate(&measurement, &result);
    assert(!result.runtime_largest_block_passed && !result.thresholds_passed);

    measurement = passing_measurement();
    measurement.codec_required_heap_bytes = SIZE_MAX;
    sonic_memory_evaluate(&measurement, &result);
    assert(!result.pre_codec_passed && !result.thresholds_passed);

    sonic_memory_evaluate(0, &result);
    assert(!result.pre_codec_passed && !result.thresholds_passed);
    return 0;
}
