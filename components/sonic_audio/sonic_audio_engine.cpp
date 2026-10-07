#include "sonic_audio_engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sonic_audio {

sonic_error_t Engine::initialize(const Operations &operations,
                                 sonic_ggwave_profile::Candidate candidate) {
    if (operations.configure == nullptr || operations.set_volume == nullptr ||
        operations.largest_free_block == nullptr || operations.read == nullptr ||
        operations.write == nullptr || operations.now_us == nullptr) {
        state_ = State::Error;
        return SONIC_AUDIO_INIT_FAILED;
    }
    operations_ = operations;
    diagnostics_.candidate = candidate;
    if (!operations_.configure(operations_.context, 24000u, 16u, 1u)) {
        state_ = State::Error;
        return SONIC_AUDIO_INIT_FAILED;
    }

    const int required_heap = sonic_codec::Codec::required_heap_bytes(candidate);
    const size_t required_bytes = required_heap > 0
        ? static_cast<size_t>(required_heap) : 0u;
    const size_t reserve_bytes = sonic_codec::kMinimumFreeHeapReserveBytes;
    const size_t largest = operations_.largest_free_block(operations_.context);

    diagnostics_.codec_required_heap_bytes = required_bytes;
    diagnostics_.codec_required_with_reserve_bytes = required_bytes + reserve_bytes;
    diagnostics_.pre_codec_largest_free_block_bytes = largest;
    diagnostics_.codec_preflight_measured = true;
    diagnostics_.codec_preflight_passed = required_bytes > 0u &&
        largest >= diagnostics_.codec_required_with_reserve_bytes;
    diagnostics_.state = State::Error;
    if (codec_.initialize(candidate, largest) != sonic_codec::Status::Ok) {
        state_ = State::Error;
        return SONIC_AUDIO_INIT_FAILED;
    }
    state_ = State::Idle;
    diagnostics_.codec_heap_bytes = codec_.heap_bytes();
    diagnostics_.state = state_;
    return SONIC_OK;
}

Event Engine::make_event(EventType type, sonic_error_t error) const {
    Event event;
    event.type = type;
    event.state = state_;
    event.error = error;
    event.frame_index = tx_frame_index_;
    event.frame_count = tx_frame_count_;
    return event;
}

Event Engine::start_rx() {
    if (state_ != State::Idle && state_ != State::Rx) {
        return make_event(EventType::Error, SONIC_INVALID_ARGUMENT);
    }
    if (codec_.reset_rx() != sonic_codec::Status::Ok) {
        state_ = State::Error;
        return make_event(EventType::Error, SONIC_AUDIO_INIT_FAILED);
    }
    state_ = State::Rx;
    diagnostics_.state = state_;
    return make_event(EventType::None);
}

Event Engine::stop_rx() {
    if (state_ != State::Rx && state_ != State::Idle) {
        return make_event(EventType::Error, SONIC_INVALID_ARGUMENT);
    }
    state_ = State::Idle;
    diagnostics_.state = state_;
    return make_event(EventType::None);
}

Event Engine::read_rx_once() {
    if (state_ != State::Rx) {
        return make_event(EventType::Error, SONIC_INVALID_ARGUMENT);
    }
    if (!operations_.read(operations_.context, rx_samples_, kSamplesPerBlock)) {
        state_ = State::Error;
        diagnostics_.state = state_;
        return make_event(EventType::Error, SONIC_AUDIO_READ_FAILED);
    }
    return process_rx_block(rx_samples_, kSamplesPerBlock);
}

float Engine::microphone_rms(const int16_t *samples, size_t count) {
    if (samples == nullptr || count == 0u) {
        return 0.0f;
    }
    uint64_t sum_squares = 0u;
    for (size_t i = 0; i < count; ++i) {
        const int32_t value = samples[i];
        sum_squares += static_cast<uint64_t>(value * value);
    }
    return static_cast<float>(std::sqrt(static_cast<double>(sum_squares) / count));
}

void Engine::update_processing_statistics(uint64_t elapsed_us) {
    ++diagnostics_.rx_processing_count;
    processing_sum_us_ += elapsed_us;
    diagnostics_.rx_processing_average_us = processing_sum_us_ / diagnostics_.rx_processing_count;
    diagnostics_.rx_processing_max_us = std::max(diagnostics_.rx_processing_max_us, elapsed_us);
    processing_samples_[processing_sample_cursor_] = elapsed_us;
    processing_sample_cursor_ = (processing_sample_cursor_ + 1u) % kTimingWindowSize;
    processing_sample_count_ = std::min(processing_sample_count_ + 1u, kTimingWindowSize);
    uint64_t sorted[kTimingWindowSize];
    std::copy(processing_samples_, processing_samples_ + processing_sample_count_, sorted);
    std::sort(sorted, sorted + processing_sample_count_);
    const size_t p99_index = (processing_sample_count_ * 99u + 99u) / 100u - 1u;
    diagnostics_.rx_processing_p99_us = sorted[p99_index];
}

Event Engine::process_rx_block(const int16_t *samples, size_t sample_count) {
    if (state_ != State::Rx || samples == nullptr || sample_count != kSamplesPerBlock) {
        return make_event(EventType::Error, SONIC_INVALID_ARGUMENT);
    }
    diagnostics_.microphone_rms = microphone_rms(samples, sample_count);
    diagnostics_.microphone_dbfs = diagnostics_.microphone_rms > 0.0f
        ? 20.0f * std::log10(diagnostics_.microphone_rms / 32768.0f)
        : -120.0f;
    uint8_t frame[sonic_codec::kFrameBytes];
    const uint64_t start = operations_.now_us(operations_.context);
    const sonic_codec::Status status = codec_.feed_rx(samples, sample_count, frame);
    const uint64_t end = operations_.now_us(operations_.context);
    update_processing_statistics(end >= start ? end - start : 0u);
    if (status == sonic_codec::Status::NoFrame) {
        return make_event(EventType::None);
    }
    if (status != sonic_codec::Status::Ok) {
        state_ = State::Error;
        diagnostics_.state = state_;
        const sonic_error_t error = status == sonic_codec::Status::InvalidArgument
            ? SONIC_INVALID_ARGUMENT : SONIC_AUDIO_READ_FAILED;
        return make_event(EventType::Error, error);
    }
    Event event = make_event(EventType::FrameReceived);
    std::memcpy(event.frame, frame, sizeof(frame));
    return event;
}

sonic_error_t Engine::prepare_current_frame() {
    const sonic_codec::Status status = codec_.prepare_tx(
        tx_frames_[tx_frame_index_], sonic_codec::kFrameBytes, 75,
        &active_tones_, &active_protocol_);
    if (status != sonic_codec::Status::Ok || active_tones_ == nullptr || active_protocol_ == nullptr ||
        !sonic_tone_renderer::begin(&renderer_, *active_tones_, *active_protocol_,
                                    24000.0, 512, 75)) {
        return SONIC_AUDIO_WRITE_FAILED;
    }
    return SONIC_OK;
}

Event Engine::begin_tx(const uint8_t frames[kTxFrameLimit][sonic_codec::kFrameBytes],
                       size_t frame_count,
                       int volume_percent,
                       bool resume_rx) {
    if ((state_ != State::Idle && state_ != State::Rx) || frames == nullptr || frame_count == 0u ||
        frame_count > kTxFrameLimit || volume_percent < 0 || volume_percent > 100) {
        return make_event(EventType::Error, SONIC_INVALID_ARGUMENT);
    }
    if (codec_.reset_rx() != sonic_codec::Status::Ok) {
        state_ = State::Error;
        diagnostics_.state = state_;
        return make_event(EventType::Error, SONIC_AUDIO_INIT_FAILED);
    }
    if (!operations_.set_volume(operations_.context, static_cast<uint8_t>(volume_percent))) {
        state_ = State::Error;
        diagnostics_.state = state_;
        return make_event(EventType::Error, SONIC_AUDIO_WRITE_FAILED);
    }
    std::memcpy(tx_frames_, frames, frame_count * sonic_codec::kFrameBytes);
    tx_frame_count_ = frame_count;
    tx_frame_index_ = 0u;
    diagnostics_.speaker_volume_percent = static_cast<uint8_t>(volume_percent);
    resume_rx_ = resume_rx;
    tx_cancelled_ = false;
    silence_samples_remaining_ = 0u;
    silence_purpose_ = SilencePurpose::None;
    diagnostics_.tx_frame_index = 0u;
    diagnostics_.tx_frame_count = frame_count;
    diagnostics_.tx_elapsed_us = 0u;
    diagnostics_.tx_inter_fragment_silence_samples = 0u;
    diagnostics_.tx_guard_silence_samples = 0u;
    tx_started_us_ = operations_.now_us(operations_.context);
    const sonic_error_t prepared = prepare_current_frame();
    if (prepared != SONIC_OK) {
        state_ = State::Error;
        diagnostics_.state = state_;
        return make_event(EventType::Error, prepared);
    }
    state_ = State::Tx;
    diagnostics_.state = state_;
    return make_event(EventType::TxProgress);
}

void Engine::finish_tx_guard(Event *event) {
    (void) sonic_tone_renderer::reset(&renderer_);
    if (codec_.reset_rx() != sonic_codec::Status::Ok) {
        state_ = State::Error;
        diagnostics_.state = state_;
        *event = make_event(EventType::Error, SONIC_AUDIO_INIT_FAILED);
        return;
    }
    state_ = resume_rx_ ? State::Rx : State::Idle;
    diagnostics_.state = state_;
    const uint64_t now = operations_.now_us(operations_.context);
    diagnostics_.tx_elapsed_us = now >= tx_started_us_ ? now - tx_started_us_ : 0u;
    *event = make_event(tx_cancelled_ ? EventType::TxCancelled : EventType::TxComplete);
}

Event Engine::write_silence_step() {
    const size_t count = std::min(silence_samples_remaining_, kTxChunkSamples);
    std::fill(chunk_, chunk_ + count, 0);
    if (!operations_.write(operations_.context, chunk_, count)) {
        state_ = State::Error;
        diagnostics_.state = state_;
        return make_event(EventType::Error, SONIC_AUDIO_WRITE_FAILED);
    }
    silence_samples_remaining_ -= count;
    if (silence_purpose_ == SilencePurpose::BetweenFrames) {
        diagnostics_.tx_inter_fragment_silence_samples += count;
    } else {
        diagnostics_.tx_guard_silence_samples += count;
    }
    if (silence_samples_remaining_ != 0u) {
        return make_event(EventType::TxProgress);
    }

    if (silence_purpose_ == SilencePurpose::BetweenFrames) {
        ++tx_frame_index_;
        diagnostics_.tx_frame_index = tx_frame_index_;
        silence_purpose_ = SilencePurpose::None;
        const sonic_error_t prepared = prepare_current_frame();
        if (prepared != SONIC_OK) {
            state_ = State::Error;
            diagnostics_.state = state_;
            return make_event(EventType::Error, prepared);
        }
        return make_event(EventType::TxProgress);
    }

    Event event;
    finish_tx_guard(&event);
    silence_purpose_ = SilencePurpose::None;
    return event;
}

Event Engine::tx_step(bool cancel_requested) {
    if (state_ != State::Tx && state_ != State::Stopping) {
        return make_event(EventType::Error, SONIC_INVALID_ARGUMENT);
    }
    if (state_ == State::Tx && cancel_requested) {
        tx_cancelled_ = true;
        (void) sonic_tone_renderer::reset(&renderer_);
        state_ = State::Stopping;
        diagnostics_.state = state_;
        silence_samples_remaining_ = kGuardSamples;
        silence_purpose_ = SilencePurpose::FinalGuard;
        return make_event(EventType::TxProgress);
    }
    if (state_ == State::Stopping || silence_purpose_ != SilencePurpose::None) {
        return write_silence_step();
    }

    size_t written = 0u;
    const sonic_tone_renderer::Status status = sonic_tone_renderer::render_next(
        &renderer_, chunk_, kTxChunkSamples, &written, false);
    if ((status != sonic_tone_renderer::Status::ChunkReady &&
         status != sonic_tone_renderer::Status::Complete) || written == 0u) {
        state_ = State::Error;
        diagnostics_.state = state_;
        return make_event(EventType::Error, SONIC_AUDIO_WRITE_FAILED);
    }
    if (!operations_.write(operations_.context, chunk_, written)) {
        state_ = State::Error;
        diagnostics_.state = state_;
        return make_event(EventType::Error, SONIC_AUDIO_WRITE_FAILED);
    }
    if (status == sonic_tone_renderer::Status::Complete) {
        if (tx_frame_index_ + 1u < tx_frame_count_) {
            silence_samples_remaining_ = kGuardSamples;
            silence_purpose_ = SilencePurpose::BetweenFrames;
        } else {
            state_ = State::Stopping;
            diagnostics_.state = state_;
            silence_samples_remaining_ = kGuardSamples;
            silence_purpose_ = SilencePurpose::FinalGuard;
        }
    }
    return make_event(EventType::TxProgress);
}

Diagnostics Engine::diagnostics() const {
    Diagnostics snapshot = diagnostics_;
    snapshot.state = state_;
    snapshot.codec_heap_bytes = codec_.heap_bytes();
    return snapshot;
}

void Engine::set_worker_stack_high_water(uint32_t words) {
    diagnostics_.worker_stack_high_water_words = words;
}

}  // namespace sonic_audio
