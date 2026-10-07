#pragma once

#include <cstddef>
#include <cstdint>

#include "sonic_codec.h"
#include "sonic_core.h"
#include "sonic_tone_renderer.h"

namespace sonic_audio {

constexpr size_t kSamplesPerBlock = 512;
constexpr size_t kTxChunkSamples = 256;
constexpr size_t kTxFrameLimit = SONIC_MAX_FRAGMENTS;
constexpr size_t kTimingWindowSize = 128;
constexpr size_t kGuardSamples = 4800;  // 200 ms at 24 kHz.
constexpr int kInitialWorkerStackBytes = 8192;

enum class State : uint8_t { Idle, Rx, Tx, Stopping, Error };
enum class EventType : uint8_t {
    None, StateChanged, FrameReceived, TxProgress, TxComplete, TxCancelled, Error
};

struct Event {
    EventType type = EventType::None;
    State state = State::Idle;
    sonic_error_t error = SONIC_OK;
    size_t frame_index = 0;
    size_t frame_count = 0;
    uint8_t frame[sonic_codec::kFrameBytes]{};
};

struct Operations {
    void *context = nullptr;
    bool (*configure)(void *context, uint32_t hz, uint8_t bits, uint8_t channels) = nullptr;
    bool (*set_volume)(void *context, uint8_t percent) = nullptr;
    size_t (*largest_free_block)(void *context) = nullptr;
    bool (*read)(void *context, int16_t *samples, size_t sample_count) = nullptr;
    bool (*write)(void *context, const int16_t *samples, size_t sample_count) = nullptr;
    uint64_t (*now_us)(void *context) = nullptr;
};

struct Diagnostics {
    uint32_t sample_rate_hz = 24000;
    uint8_t sample_bits = 16;
    uint8_t channels = 1;
    State state = State::Idle;
    sonic_ggwave_profile::Candidate candidate = sonic_ggwave_profile::Candidate::AudibleFastest;
    uint8_t frame_bytes = sonic_codec::kFrameBytes;
    bool dss_enabled = true;
    uint8_t speaker_volume_percent = 75;
    bool codec_preflight_measured = false;
    bool codec_preflight_passed = false;
    size_t codec_required_heap_bytes = 0;
    size_t codec_required_with_reserve_bytes = 0;
    size_t pre_codec_largest_free_block_bytes = 0;
    float microphone_rms = 0.0f;
    float microphone_dbfs = -120.0f;
    uint32_t worker_stack_high_water_words = 0;
    int codec_heap_bytes = 0;
    uint64_t rx_processing_count = 0;
    uint64_t rx_processing_average_us = 0;
    uint64_t rx_processing_p99_us = 0;
    uint64_t rx_processing_max_us = 0;
    size_t tx_frame_index = 0;
    size_t tx_frame_count = 0;
    uint64_t tx_elapsed_us = 0;
    uint64_t tx_inter_fragment_silence_samples = 0;
    uint64_t tx_guard_silence_samples = 0;
};

// Portable owner/state engine. The ESP-IDF worker is the only caller in the
// firmware; host tests inject I/O operations to prove transitions and guards.
class Engine {
public:
    Engine() = default;
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;

    sonic_error_t initialize(const Operations &operations,
                             sonic_ggwave_profile::Candidate candidate =
                                 sonic_ggwave_profile::Candidate::AudibleFastest);
    Event start_rx();
    Event stop_rx();
    Event read_rx_once();
    Event process_rx_block(const int16_t *samples, size_t sample_count);
    Event begin_tx(const uint8_t frames[kTxFrameLimit][sonic_codec::kFrameBytes],
                   size_t frame_count,
                   int volume_percent,
                   bool resume_rx);
    Event tx_step(bool cancel_requested);
    Diagnostics diagnostics() const;
    void set_worker_stack_high_water(uint32_t words);
    State state() const { return state_; }

private:
    enum class SilencePurpose : uint8_t { None, BetweenFrames, FinalGuard };

    Event make_event(EventType type, sonic_error_t error = SONIC_OK) const;
    sonic_error_t prepare_current_frame();
    Event write_silence_step();
    void finish_tx_guard(Event *event);
    void update_processing_statistics(uint64_t elapsed_us);
    static float microphone_rms(const int16_t *samples, size_t count);

    Operations operations_{};
    sonic_codec::Codec codec_;
    sonic_tone_renderer::Renderer renderer_{};
    const GGWave::Tones *active_tones_ = nullptr;
    const GGWave::Protocol *active_protocol_ = nullptr;
    int16_t chunk_[kTxChunkSamples]{};
    int16_t rx_samples_[kSamplesPerBlock]{};
    uint8_t tx_frames_[kTxFrameLimit][sonic_codec::kFrameBytes]{};
    size_t tx_frame_count_ = 0;
    size_t tx_frame_index_ = 0;
    bool resume_rx_ = false;
    bool tx_cancelled_ = false;
    size_t silence_samples_remaining_ = 0;
    SilencePurpose silence_purpose_ = SilencePurpose::None;
    uint64_t tx_started_us_ = 0;
    State state_ = State::Idle;
    Diagnostics diagnostics_{};
    uint64_t processing_sum_us_ = 0;
    uint64_t processing_samples_[kTimingWindowSize]{};
    size_t processing_sample_count_ = 0;
    size_t processing_sample_cursor_ = 0;
};

}  // namespace sonic_audio
