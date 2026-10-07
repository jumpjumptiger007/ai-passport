#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "sonic_ggwave_profile.h"

namespace {
GGWave * instances[GGWAVE_MAX_INSTANCES]{};
struct InputResampler { GGWave::Resampler value; void * heap = nullptr; float factor = 1.0f; };
constexpr sonic_ggwave_profile::Candidate kCandidate =
    sonic_ggwave_profile::Candidate::AudibleFastest;

GGWave * get(int id) {
    return id >= 0 && id < GGWAVE_MAX_INSTANCES ? instances[id] : nullptr;
}

int create(float actual_rate, bool receive) {
    if (actual_rate < 8000.0f || actual_rate > 192000.0f ||
        !sonic_ggwave_profile::configure_protocols(kCandidate)) return -1;
    GGWave::Parameters p = GGWave::getDefaultParameters();
    p.payloadLength = 40;
    p.sampleRateInp = actual_rate;
    p.sampleRateOut = actual_rate;
    p.sampleRate = 24000.0f;
    p.samplesPerFrame = 512;
    p.sampleFormatInp = GGWAVE_SAMPLE_FORMAT_I16;
    p.sampleFormatOut = GGWAVE_SAMPLE_FORMAT_I16;
    p.operatingMode = (receive ? GGWAVE_OPERATING_MODE_RX : GGWAVE_OPERATING_MODE_TX) |
                      GGWAVE_OPERATING_MODE_USE_DSS;
    for (int id = 0; id < GGWAVE_MAX_INSTANCES; ++id) {
        if (instances[id] == nullptr) {
            auto * instance = new GGWave();
            if (!instance->prepare(p, true)) { delete instance; return -1; }
            instances[id] = instance;
            return id;
        }
    }
    return -1;
}
}

extern "C" {
void sl_ggwave_quiet() { GGWave::setLogFile(nullptr); }
int sl_ggwave_create_rx(float actual_rate) { return create(actual_rate, true); }
int sl_ggwave_create_tx(float actual_rate) { return create(actual_rate, false); }
void sl_ggwave_free(int id) { if (auto * instance = get(id)) { delete instance; instances[id] = nullptr; } }

InputResampler * sl_ggwave_create_input_resampler(float input_rate, float output_rate) {
    if (input_rate < 8000.0f || output_rate < 8000.0f ||
        input_rate > 192000.0f || output_rate > 192000.0f || input_rate == output_rate) return nullptr;
    auto * result = new InputResampler();
    int heap_size = 0;
    if (!result->value.alloc(nullptr, heap_size) || heap_size <= 0) { delete result; return nullptr; }
    result->heap = std::calloc(static_cast<size_t>(heap_size), 1);
    if (!result->heap) { delete result; return nullptr; }
    int heap_offset = 0;
    if (!result->value.alloc(result->heap, heap_offset)) { std::free(result->heap); delete result; return nullptr; }
    result->factor = input_rate / output_rate;
    return result;
}

int sl_ggwave_resample_input(InputResampler * resampler, const int16_t * input,
                             int input_count, int16_t * output, int output_capacity) {
    if (!resampler || !input || !output || input_count <= GGWave::Resampler::kWidth ||
        input_count > 4096 - GGWave::Resampler::kWidth || output_capacity <= 0 ||
        output_capacity < static_cast<int>(input_count / resampler->factor) + 2) return -1;
    std::vector<float> source(static_cast<size_t>(input_count));
    std::vector<float> converted(static_cast<size_t>(output_capacity));
    for (int i = 0; i < input_count; ++i) source[static_cast<size_t>(i)] = static_cast<float>(input[i]) / 32768.0f;
    const int count = resampler->value.resample(resampler->factor, input_count, source.data(), converted.data());
    if (count < 0 || count > output_capacity) return -1;
    for (int i = 0; i < count; ++i) {
        const float sample = converted[static_cast<size_t>(i)];
        const float clipped = sample < -1.0f ? -1.0f : sample > 1.0f ? 1.0f : sample;
        output[i] = static_cast<int16_t>(clipped * (clipped < 0.0f ? 32768.0f : 32767.0f));
    }
    return count;
}

void sl_ggwave_free_input_resampler(InputResampler * resampler) {
    if (resampler) { std::free(resampler->heap); delete resampler; }
}

int sl_ggwave_encode(int id, const uint8_t * frame, int16_t ** samples) {
    GGWave * instance = get(id);
    GGWave::ProtocolId protocol;
    if (!instance || !frame || !samples ||
        !sonic_ggwave_profile::protocol_id(kCandidate, &protocol) ||
        !instance->init(40, reinterpret_cast<const char *>(frame), protocol, 35)) return -1;
    const uint32_t bytes = instance->encode();
    if (!bytes || bytes % sizeof(int16_t)) return -1;
    auto * output = static_cast<int16_t *>(std::malloc(bytes));
    if (!output) return -1;
    std::memcpy(output, instance->txWaveform(), bytes);
    *samples = output;
    return static_cast<int>(bytes / sizeof(int16_t));
}

void sl_ggwave_free_samples(int16_t * samples) { std::free(samples); }

int sl_ggwave_decode(int id, const int16_t * samples, int sample_count,
                     uint8_t * payload, int payload_capacity) {
    GGWave * instance = get(id);
    if (!instance || !samples || !payload || sample_count <= 0 || payload_capacity < 40) return -1;
    if (!instance->decode(samples, static_cast<uint32_t>(sample_count * sizeof(int16_t)))) return -1;
    if (instance->rxDataLength() <= 0) return 0;
    GGWave::TxRxData data;
    const int length = instance->rxTakeData(data);
    if (length <= 0 || length > payload_capacity) return -1;
    std::memcpy(payload, data.data(), static_cast<size_t>(length));
    return length;
}
}
