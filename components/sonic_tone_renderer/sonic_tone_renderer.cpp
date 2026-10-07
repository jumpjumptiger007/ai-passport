#include "sonic_tone_renderer.h"

#include <cmath>
#include <limits>

namespace sonic_tone_renderer {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kFadeFraction = 0.15;

bool validate_tone_plan(const GGWave::Tone *tones,
                        size_t tone_count,
                        const GGWave::Protocol &protocol,
                        int samples_per_frame,
                        size_t *out_group_count) {
    if (tones == nullptr || tone_count == 0 || out_group_count == nullptr ||
        protocol.extra != 1 || protocol.framesPerTx <= 0 ||
        protocol.bytesPerTx <= 0 || protocol.freqStart <= 0 ||
        protocol.nDataBitsPerTx() <= 0 || samples_per_frame <= 0) {
        return false;
    }

    const int max_tone = 2 * protocol.bytesPerTx * 16;
    const size_t tones_per_group = static_cast<size_t>(protocol.nTones());
    size_t groups = 0;
    size_t cursor = 0;

    while (cursor < tone_count) {
        size_t group_size = 0;
        while (cursor < tone_count && tones[cursor] != -1) {
            const int tone = static_cast<int>(tones[cursor]);
            const int bin = static_cast<int>(protocol.freqStart) + tone;
            if (tone < 0 || tone >= max_tone || bin <= 0) {
                return false;
            }
            ++group_size;
            ++cursor;
        }

        if (group_size == 0 || group_size != tones_per_group || cursor >= tone_count || tones[cursor] != -1) {
            return false;
        }
        ++groups;
        ++cursor;
    }

    *out_group_count = groups;
    return groups > 0;
}

bool load_group(Renderer *renderer) {
    if (renderer->tone_cursor >= renderer->tone_count) {
        renderer->group_start = renderer->tone_count;
        renderer->group_end = renderer->tone_count;
        return false;
    }

    renderer->group_start = renderer->tone_cursor;
    while (renderer->tone_cursor < renderer->tone_count && renderer->tones[renderer->tone_cursor] != -1) {
        ++renderer->tone_cursor;
    }
    renderer->group_end = renderer->tone_cursor;
    ++renderer->tone_cursor;  // skip the required group separator
    renderer->sample_in_group = 0;
    return renderer->group_end > renderer->group_start;
}

double edge_gain(size_t sample_in_group, size_t group_samples) {
    const double fade_samples = kFadeFraction * static_cast<double>(group_samples);
    const size_t fade_in_end = static_cast<size_t>(kFadeFraction * static_cast<double>(group_samples));
    const size_t fade_out_start = static_cast<size_t>((1.0 - kFadeFraction) * static_cast<double>(group_samples));

    if (sample_in_group < fade_in_end) {
        return static_cast<double>(sample_in_group) / fade_samples;
    }
    if (sample_in_group > fade_out_start) {
        return static_cast<double>(group_samples - sample_in_group) / fade_samples;
    }
    return 1.0;
}

int16_t to_i16(double sample) {
    double scaled = sample * 32768.0;
    if (scaled > 32767.0) {
        scaled = 32767.0;
    } else if (scaled < -32768.0) {
        scaled = -32768.0;
    }
    return static_cast<int16_t>(scaled);
}

}  // namespace

bool begin(Renderer *renderer,
           const GGWave::Tones &tones,
           const GGWave::Protocol &protocol,
           double sample_rate_hz,
           int samples_per_frame,
           int volume_percent) {
    if (renderer == nullptr) {
        return false;
    }
    reset(renderer);

    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0 ||
        samples_per_frame <= 0 || volume_percent < 0 || volume_percent > 100) {
        return false;
    }

    size_t group_count = 0;
    if (!validate_tone_plan(tones.data(), tones.size(), protocol, samples_per_frame, &group_count)) {
        return false;
    }

    const size_t samples_per_group = static_cast<size_t>(protocol.framesPerTx) *
                                     static_cast<size_t>(samples_per_frame);
    if (samples_per_group == 0 || group_count > std::numeric_limits<size_t>::max() / samples_per_group) {
        return false;
    }

    renderer->tones = tones.data();
    renderer->tone_count = tones.size();
    renderer->protocol = protocol;
    renderer->sample_rate_hz = sample_rate_hz;
    renderer->samples_per_frame = samples_per_frame;
    renderer->volume_percent = volume_percent;
    renderer->group_count = group_count;
    renderer->total_samples = group_count * samples_per_group;
    renderer->valid = true;
    return load_group(renderer);
}

Status render_next(Renderer *renderer,
                   int16_t *destination,
                   size_t capacity,
                   size_t *written,
                   bool cancel_requested) {
    if (written != nullptr) {
        *written = 0;
    }
    if (renderer == nullptr || written == nullptr || destination == nullptr || capacity == 0 || !renderer->valid) {
        return Status::InvalidArgument;
    }
    if (renderer->samples_rendered >= renderer->total_samples) {
        return Status::Complete;
    }
    if (cancel_requested) {
        return Status::Cancelled;
    }

    const size_t samples_per_group = static_cast<size_t>(renderer->protocol.framesPerTx) *
                                     static_cast<size_t>(renderer->samples_per_frame);
    const double hz_per_bin = renderer->sample_rate_hz / static_cast<double>(renderer->samples_per_frame);
    const double volume = static_cast<double>(renderer->volume_percent) / 100.0;
    size_t count = 0;

    while (count < capacity && renderer->samples_rendered < renderer->total_samples) {
        if (renderer->sample_in_group >= samples_per_group) {
            if (!load_group(renderer)) {
                renderer->valid = false;
                return Status::InvalidTonePlan;
            }
        }

        double sum = 0.0;
        const size_t sample_in_frame = renderer->sample_in_group %
                                       static_cast<size_t>(renderer->samples_per_frame);
        for (size_t i = renderer->group_start; i < renderer->group_end; ++i) {
            const int tone = static_cast<int>(renderer->tones[i]);
            // The tone selects an adjacent FFT bin; the donor phase is shared
            // by the pair of bins belonging to one encoded bit.
            const int phase_index = tone / 2;
            const double frequency_hz = static_cast<double>(frequency_bin_for_tone(tone, renderer->protocol.freqStart)) * hz_per_bin;
            const double phase_offset = kPi * static_cast<double>(phase_index) /
                                        static_cast<double>(renderer->protocol.nDataBitsPerTx());
            const double sample_time_seconds = static_cast<double>(sample_in_frame) / renderer->sample_rate_hz;
            sum += volume * std::sin((2.0 * kPi) * sample_time_seconds * frequency_hz + phase_offset);
        }

        const size_t simultaneous_tones = renderer->group_end - renderer->group_start;
        const double normalized = sum / static_cast<double>(simultaneous_tones);
        destination[count] = to_i16(normalized * edge_gain(renderer->sample_in_group, samples_per_group));

        ++count;
        ++renderer->sample_in_group;
        ++renderer->samples_rendered;
    }

    *written = count;
    return renderer->samples_rendered == renderer->total_samples ? Status::Complete : Status::ChunkReady;
}

void reset(Renderer *renderer) {
    if (renderer != nullptr) {
        *renderer = Renderer{};
    }
}

int phase_index_for_tone(int tone_index) {
    return tone_index < 0 ? -1 : tone_index / 2;
}

int frequency_bin_for_tone(int tone_index, int frequency_start) {
    return tone_index < 0 || frequency_start < 0 ? -1 : frequency_start + tone_index;
}

}  // namespace sonic_tone_renderer
