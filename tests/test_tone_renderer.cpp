#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "sonic_ggwave_profile.h"
#include "sonic_golden_vectors.h"
#include "sonic_tone_renderer.h"

namespace {

using sonic_ggwave_profile::Candidate;
using sonic_tone_renderer::Renderer;
using sonic_tone_renderer::Status;

const sonic_golden_vector_t *find_vector(const char *name) {
    for (size_t i = 0; i < sonic_golden_vector_count; ++i) {
        if (std::strcmp(sonic_golden_vectors[i].name, name) == 0) {
            return &sonic_golden_vectors[i];
        }
    }
    return nullptr;
}

size_t count_groups(const GGWave::Tones &tones) {
    size_t count = 0;
    for (int i = 0; i < tones.size(); ++i) {
        if (tones[i] == -1) {
            ++count;
        }
    }
    return count;
}

std::vector<int16_t> render(const GGWave::Tones &tones,
                            const GGWave::Protocol &protocol,
                            int sample_rate,
                            int samples_per_frame,
                            int volume,
                            size_t chunk_capacity,
                            size_t *out_total_samples = nullptr) {
    Renderer renderer;
    assert(sonic_tone_renderer::begin(&renderer, tones, protocol, sample_rate, samples_per_frame, volume));
    if (out_total_samples != nullptr) {
        *out_total_samples = renderer.total_samples;
    }

    std::array<int16_t, 512> scratch{};
    std::vector<int16_t> result;
    bool complete = false;
    while (!complete) {
        size_t written = 0;
        const Status status = sonic_tone_renderer::render_next(
            &renderer, scratch.data(), chunk_capacity, &written, false);
        assert(written <= chunk_capacity);
        result.insert(result.end(), scratch.begin(), scratch.begin() + static_cast<std::ptrdiff_t>(written));
        if (status == Status::Complete) {
            complete = true;
        } else {
            assert(status == Status::ChunkReady);
            assert(written > 0);
        }
    }
    assert(result.size() == renderer.total_samples);
    return result;
}

bool stock_rx_decodes(const std::vector<int16_t> &pcm,
                      const uint8_t *expected,
                      size_t expected_length,
                      Candidate candidate) {
    GGWave receiver;
    assert(sonic_ggwave_profile::prepare(receiver, candidate));

    constexpr size_t kInputChunkSamples = 512;
    size_t cursor = 0;
    while (cursor < pcm.size()) {
        const size_t count = (pcm.size() - cursor < kInputChunkSamples)
                                 ? pcm.size() - cursor
                                 : kInputChunkSamples;
        assert(receiver.decode(pcm.data() + cursor,
                               static_cast<uint32_t>(count * sizeof(int16_t))));
        cursor += count;

        const int decoded_length = receiver.rxDataLength();
        if (decoded_length > 0) {
            if (static_cast<size_t>(decoded_length) != expected_length) {
                return false;
            }
            return std::memcmp(receiver.rxData().data(), expected, expected_length) == 0;
        }
        if (decoded_length < 0) {
            return false;
        }
    }
    return false;
}

std::vector<int16_t> stock_full_waveform(const uint8_t frame[SONIC_FRAME_SIZE], Candidate candidate) {
    assert(sonic_ggwave_profile::configure_protocols(candidate));
    GGWave::Parameters parameters = sonic_ggwave_profile::passport_parameters();
    parameters.operatingMode &= ~GGWAVE_OPERATING_MODE_TX_ONLY_TONES;
    GGWave transmitter;
    assert(transmitter.prepare(parameters, true));
    GGWave::ProtocolId protocol_id = GGWAVE_PROTOCOL_COUNT;
    assert(sonic_ggwave_profile::protocol_id(candidate, &protocol_id));
    assert(transmitter.init(SONIC_FRAME_SIZE,
                            reinterpret_cast<const char *>(frame),
                            protocol_id,
                            60));
    const size_t encoded_bytes = transmitter.encode();
    const size_t count = encoded_bytes / sizeof(int16_t);
    const int16_t *samples = static_cast<const int16_t *>(transmitter.txWaveform());
    assert(samples != nullptr && count > 0);
    return std::vector<int16_t>(samples, samples + count);
}

void assert_matches_stock_waveform(const std::vector<int16_t> &rendered,
                                   const uint8_t frame[SONIC_FRAME_SIZE],
                                   Candidate candidate) {
    const std::vector<int16_t> reference = stock_full_waveform(frame, candidate);
    assert(reference.size() == rendered.size());
    int max_difference = 0;
    for (size_t i = 0; i < rendered.size(); ++i) {
        const int difference = static_cast<int>(reference[i]) - static_cast<int>(rendered[i]);
        const int absolute = difference < 0 ? -difference : difference;
        if (absolute > max_difference) {
            max_difference = absolute;
        }
    }
    // The renderer clamps before I16 conversion; the donor truncates. Apart
    // from that final quantization step, synthesis should follow stock output.
    assert(max_difference <= 2);
}

void assert_frame_parity(const uint8_t frame[SONIC_FRAME_SIZE], Candidate candidate, bool test_chunk_invariance) {
    GGWave::ProtocolId protocol_id = GGWAVE_PROTOCOL_COUNT;
    assert(sonic_ggwave_profile::protocol_id(candidate, &protocol_id));

    GGWave transmitter;
    assert(sonic_ggwave_profile::prepare(transmitter, candidate));
    assert(transmitter.init(SONIC_FRAME_SIZE,
                            reinterpret_cast<const char *>(frame),
                            protocol_id,
                            60));
    (void) transmitter.encode();

    const GGWave::Tones tones = transmitter.txTones();
    assert(tones.size() > 0);
    const GGWave::Protocol protocol = transmitter.txProtocols()[protocol_id];

    size_t total_samples = 0;
    const std::vector<int16_t> pcm = render(tones,
                                            protocol,
                                            static_cast<int>(transmitter.sampleRateOut()),
                                            transmitter.samplesPerFrame(),
                                            60,
                                            512,
                                            &total_samples);
    assert(total_samples == count_groups(tones) *
                                static_cast<size_t>(protocol.framesPerTx) *
                                static_cast<size_t>(transmitter.samplesPerFrame()));
    assert_matches_stock_waveform(pcm, frame, candidate);
    assert(stock_rx_decodes(pcm, frame, SONIC_FRAME_SIZE, candidate));

    if (test_chunk_invariance) {
        assert(render(tones, protocol, 24000, 512, 60, 256) == pcm);
        assert(render(tones, protocol, 24000, 512, 60, 137) == pcm);
    }
}

void assert_cancellation_and_reset(const uint8_t frame[SONIC_FRAME_SIZE]) {
    GGWave::ProtocolId protocol_id = GGWAVE_PROTOCOL_COUNT;
    assert(sonic_ggwave_profile::protocol_id(Candidate::AudibleFastest, &protocol_id));
    GGWave transmitter;
    assert(sonic_ggwave_profile::prepare(transmitter, Candidate::AudibleFastest));
    assert(transmitter.init(SONIC_FRAME_SIZE,
                            reinterpret_cast<const char *>(frame),
                            protocol_id,
                            60));
    (void) transmitter.encode();

    const GGWave::Tones tones = transmitter.txTones();
    const GGWave::Protocol protocol = transmitter.txProtocols()[protocol_id];
    Renderer renderer;
    assert(sonic_tone_renderer::begin(&renderer, tones, protocol, 24000.0, 512, 60));

    std::array<int16_t, 137> chunk{};
    size_t written = 0;
    assert(sonic_tone_renderer::render_next(&renderer, chunk.data(), chunk.size(), &written, false) ==
           Status::ChunkReady);
    assert(written == chunk.size());
    written = 99;
    assert(sonic_tone_renderer::render_next(&renderer, chunk.data(), chunk.size(), &written, true) ==
           Status::Cancelled);
    assert(written == 0);

    sonic_tone_renderer::reset(&renderer);
    assert(!renderer.valid);
    assert(sonic_tone_renderer::begin(&renderer, tones, protocol, 24000.0, 512, 60));
    const std::vector<int16_t> after_reset = render(tones, protocol, 24000, 512, 60, 137);
    assert(stock_rx_decodes(after_reset, frame, SONIC_FRAME_SIZE, Candidate::AudibleFastest));
}

void assert_invalid_plans(const GGWave::Protocol &protocol) {
    GGWave::Tone empty_group_data[] = {-1};
    GGWave::Tone missing_separator_data[] = {0, 2, 4, 6, 8, 10};
    GGWave::Tone out_of_range_data[] = {100, 2, 4, 6, 8, 10, -1};
    const GGWave::Tones empty_group(empty_group_data, 1);
    const GGWave::Tones missing_separator(missing_separator_data, 6);
    const GGWave::Tones out_of_range(out_of_range_data, 7);
    Renderer renderer;

    assert(!sonic_tone_renderer::begin(&renderer, empty_group, protocol, 24000.0, 512, 60));
    assert(!sonic_tone_renderer::begin(&renderer, missing_separator, protocol, 24000.0, 512, 60));
    assert(!sonic_tone_renderer::begin(&renderer, out_of_range, protocol, 24000.0, 512, 60));
}

}  // namespace

int main() {
    GGWave::setLogFile(nullptr);

    // Adjacent bins differ in frequency but share the donor's bit-pair phase.
    assert(sonic_tone_renderer::frequency_bin_for_tone(20, 40) == 60);
    assert(sonic_tone_renderer::frequency_bin_for_tone(21, 40) == 61);
    assert(sonic_tone_renderer::phase_index_for_tone(20) == 10);
    assert(sonic_tone_renderer::phase_index_for_tone(21) == 10);
    assert(sonic_tone_renderer::phase_index_for_tone(-1) == -1);

    const sonic_golden_vector_t *text_a = find_vector("text_a");
    const sonic_golden_vector_t *max_text = find_vector("max_ascii_text");
    const sonic_golden_vector_t *token_nul = find_vector("token_nul");
    assert(text_a != nullptr && text_a->frame_count == 1);
    assert(max_text != nullptr && max_text->frame_count == 3);
    assert(token_nul != nullptr && token_nul->frame_count == 1);

    for (Candidate candidate : {Candidate::AudibleFastest, Candidate::AudibleFast}) {
        for (size_t i = 0; i < text_a->frame_count; ++i) {
            assert_frame_parity(text_a->frames[i], candidate, true);
        }
        for (size_t i = 0; i < max_text->frame_count; ++i) {
            assert_frame_parity(max_text->frames[i], candidate, false);
        }
        for (size_t i = 0; i < token_nul->frame_count; ++i) {
            assert_frame_parity(token_nul->frames[i], candidate, false);
        }
    }

    GGWave::ProtocolId fastest_id = GGWAVE_PROTOCOL_COUNT;
    assert(sonic_ggwave_profile::protocol_id(Candidate::AudibleFastest, &fastest_id));
    GGWave transmitter;
    assert(sonic_ggwave_profile::prepare(transmitter, Candidate::AudibleFastest));
    assert_invalid_plans(transmitter.txProtocols()[fastest_id]);
    assert_cancellation_and_reset(text_a->frames[0]);

    std::puts("test_tone_renderer: PASS");
    return 0;
}
