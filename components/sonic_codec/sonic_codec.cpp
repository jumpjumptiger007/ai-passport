#include "sonic_codec.h"

#include <cstring>
#include <new>

namespace sonic_codec {

int Codec::required_heap_bytes(sonic_ggwave_profile::Candidate candidate) {
    int required = 0;
    return sonic_ggwave_profile::required_heap_bytes(candidate, &required) ? required : 0;
}

Status Codec::initialize(sonic_ggwave_profile::Candidate candidate,
                         size_t largest_free_block_bytes,
                         size_t reserve_bytes) {
    if (initialized_) {
        return Status::InvalidArgument;
    }

    int required = 0;
    if (!sonic_ggwave_profile::protocol_id(candidate, &protocol_id_) ||
        !sonic_ggwave_profile::required_heap_bytes(candidate, &required) || required <= 0) {
        return Status::InitFailed;
    }
    if (largest_free_block_bytes < static_cast<size_t>(required) + reserve_bytes) {
        return Status::InitFailed;
    }
    if (!sonic_ggwave_profile::prepare(instance_, candidate, true)) {
        return Status::InitFailed;
    }

    candidate_ = candidate;
    initialized_ = true;
    return Status::Ok;
}

Status Codec::reset_rx() {
    if (!initialized_) {
        return Status::InvalidArgument;
    }
    // The pinned donor's init() resets its RX detector/recording buffers while
    // retaining the prepared heap. A zero-length TX input leaves no TX pending.
    return instance_.init(0, nullptr, protocol_id_) ? Status::Ok : Status::InitFailed;
}

Status Codec::feed_rx(const int16_t *samples, size_t sample_count, uint8_t out_frame[kFrameBytes]) {
    if (!initialized_ || samples == nullptr || out_frame == nullptr ||
        sample_count != kRxSamplesPerBlock) {
        return Status::InvalidArgument;
    }
    if (!instance_.decode(samples, static_cast<uint32_t>(sample_count * sizeof(int16_t)))) {
        return Status::DecodeFailed;
    }

    const int length = instance_.rxDataLength();
    if (length == 0) {
        return Status::NoFrame;
    }
    if (length != static_cast<int>(kFrameBytes) ||
        instance_.rxTakeData(rx_data_) != static_cast<int>(kFrameBytes) ||
        rx_data_.size() != static_cast<int>(kFrameBytes)) {
        return Status::DecodeFailed;
    }
    std::memcpy(out_frame, rx_data_.data(), kFrameBytes);
    return Status::Ok;
}

Status Codec::prepare_tx(const uint8_t *frame,
                         size_t frame_length,
                         int volume_percent,
                         const GGWave::Tones **out_tones,
                         const GGWave::Protocol **out_protocol) {
    if (!initialized_ || frame == nullptr || frame_length != kFrameBytes ||
        volume_percent < 0 || volume_percent > 100 || out_tones == nullptr ||
        out_protocol == nullptr) {
        return Status::InvalidArgument;
    }
    if (!instance_.init(static_cast<int>(frame_length), reinterpret_cast<const char *>(frame),
                        protocol_id_, volume_percent)) {
        return Status::EncodeFailed;
    }
    (void) instance_.encode();
    tx_tones_.~ggvector<GGWave::Tone>();
    new (&tx_tones_) GGWave::Tones(instance_.txTones());
    if (tx_tones_.size() <= 0) {
        return Status::EncodeFailed;
    }
    tx_protocol_ = instance_.txProtocols()[protocol_id_];
    *out_tones = &tx_tones_;
    *out_protocol = &tx_protocol_;
    return Status::Ok;
}

}  // namespace sonic_codec
