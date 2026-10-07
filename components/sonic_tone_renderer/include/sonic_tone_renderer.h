#pragma once

#include <cstddef>
#include <cstdint>

#include "ggwave/ggwave.h"

namespace sonic_tone_renderer {

enum class Status : uint8_t {
    ChunkReady,
    Complete,
    Cancelled,
    InvalidArgument,
    InvalidTonePlan,
};

struct Renderer {
    const GGWave::Tone *tones = nullptr;
    size_t tone_count = 0;
    GGWave::Protocol protocol{};
    double sample_rate_hz = 0.0;
    int samples_per_frame = 0;
    int volume_percent = 0;
    size_t tone_cursor = 0;
    size_t group_start = 0;
    size_t group_end = 0;
    size_t sample_in_group = 0;
    size_t group_count = 0;
    size_t total_samples = 0;
    size_t samples_rendered = 0;
    bool valid = false;
};

// Starts a renderer over a stock ggwave tone plan. The tone storage remains
// owned by the caller and must outlive this renderer.
bool begin(Renderer *renderer,
           const GGWave::Tones &tones,
           const GGWave::Protocol &protocol,
           double sample_rate_hz,
           int samples_per_frame,
           int volume_percent);

// Emits at most capacity samples. Cancellation is observed before writing the
// next chunk, so an already returned chunk is always complete.
Status render_next(Renderer *renderer,
                   int16_t *destination,
                   size_t capacity,
                   size_t *written,
                   bool cancel_requested);

void reset(Renderer *renderer);

// Donor bit-pair phase mapping: adjacent tone bins share a phase offset.
int phase_index_for_tone(int tone_index);
int frequency_bin_for_tone(int tone_index, int frequency_start);

}  // namespace sonic_tone_renderer
