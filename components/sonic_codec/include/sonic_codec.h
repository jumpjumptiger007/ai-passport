#pragma once

#include <cstddef>
#include <cstdint>

#include "sonic_ggwave_profile.h"

namespace sonic_codec {

constexpr size_t kFrameBytes = 40;
constexpr size_t kRxSamplesPerBlock = 512;
constexpr size_t kMinimumFreeHeapReserveBytes = 24u * 1024u;

enum class Status : uint8_t {
    Ok,
    NoFrame,
    InvalidArgument,
    InitFailed,
    DecodeFailed,
    EncodeFailed,
};

// Owns one fixed-profile donor instance for its full lifetime. The instance is
// reset in place when moving between RX and TX; it is never re-prepared per
// frame or mode transition.
class Codec {
public:
    Codec() = default;
    Codec(const Codec &) = delete;
    Codec &operator=(const Codec &) = delete;

    Status initialize(sonic_ggwave_profile::Candidate candidate,
                      size_t largest_free_block_bytes,
                      size_t reserve_bytes = kMinimumFreeHeapReserveBytes);
    Status reset_rx();
    Status feed_rx(const int16_t *samples, size_t sample_count, uint8_t out_frame[kFrameBytes]);
    Status prepare_tx(const uint8_t *frame,
                      size_t frame_length,
                      int volume_percent,
                      const GGWave::Tones **out_tones,
                      const GGWave::Protocol **out_protocol);

    bool initialized() const { return initialized_; }
    int heap_bytes() const { return initialized_ ? instance_.heapSize() : 0; }
    sonic_ggwave_profile::Candidate candidate() const { return candidate_; }
    static int required_heap_bytes(sonic_ggwave_profile::Candidate candidate);

private:
    GGWave instance_;
    GGWave::Tones tx_tones_;
    GGWave::Protocol tx_protocol_{};
    GGWave::TxRxData rx_data_;
    sonic_ggwave_profile::Candidate candidate_ = sonic_ggwave_profile::Candidate::AudibleFastest;
    GGWave::ProtocolId protocol_id_ = GGWAVE_PROTOCOL_COUNT;
    bool initialized_ = false;
};

}  // namespace sonic_codec
