#include <array>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "sonic_codec.h"
#include "sonic_golden_vectors.h"
#include "sonic_tone_renderer.h"

namespace {

const sonic_golden_vector_t *find_vector(const char *name) {
    for (size_t i = 0; i < sonic_golden_vector_count; ++i) {
        if (std::strcmp(sonic_golden_vectors[i].name, name) == 0) {
            return &sonic_golden_vectors[i];
        }
    }
    return nullptr;
}

std::vector<int16_t> render(const GGWave::Tones &tones, const GGWave::Protocol &protocol) {
    sonic_tone_renderer::Renderer renderer;
    assert(sonic_tone_renderer::begin(&renderer, tones, protocol, 24000.0, 512, 60));
    std::array<int16_t, 256> chunk{};
    std::vector<int16_t> pcm;
    for (;;) {
        size_t written = 0u;
        const auto status = sonic_tone_renderer::render_next(
            &renderer, chunk.data(), chunk.size(), &written, false);
        pcm.insert(pcm.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(written));
        if (status == sonic_tone_renderer::Status::Complete) {
            break;
        }
        assert(status == sonic_tone_renderer::Status::ChunkReady && written > 0u);
    }
    return pcm;
}

}  // namespace

int main() {
    GGWave::setLogFile(nullptr);
    const auto *token = find_vector("token_nul");
    assert(token != nullptr && token->frame_count == 1u);

    sonic_codec::Codec codec;
    const int required = sonic_codec::Codec::required_heap_bytes(
        sonic_ggwave_profile::Candidate::AudibleFastest);
    assert(required > 0);
    assert(codec.initialize(sonic_ggwave_profile::Candidate::AudibleFastest,
                             static_cast<size_t>(required) + sonic_codec::kMinimumFreeHeapReserveBytes - 1u) ==
           sonic_codec::Status::InitFailed);
    assert(codec.initialize(sonic_ggwave_profile::Candidate::AudibleFastest,
                             static_cast<size_t>(required) + sonic_codec::kMinimumFreeHeapReserveBytes) ==
           sonic_codec::Status::Ok);
    const int heap_bytes = codec.heap_bytes();
    assert(heap_bytes == required);
    assert(codec.initialize(sonic_ggwave_profile::Candidate::AudibleFastest,
                             static_cast<size_t>(required) + 100000u) == sonic_codec::Status::InvalidArgument);
    assert(codec.feed_rx(nullptr, sonic_codec::kRxSamplesPerBlock, nullptr) ==
           sonic_codec::Status::InvalidArgument);
    assert(codec.reset_rx() == sonic_codec::Status::Ok);

    const GGWave::Tones *tones = nullptr;
    const GGWave::Protocol *protocol = nullptr;
    assert(codec.prepare_tx(token->frames[0], sonic_codec::kFrameBytes, 60,
                            &tones, &protocol) == sonic_codec::Status::Ok);
    assert(tones != nullptr && tones->size() > 0 && protocol != nullptr);
    const std::vector<int16_t> pcm = render(*tones, *protocol);
    assert(codec.heap_bytes() == heap_bytes);
    assert(codec.reset_rx() == sonic_codec::Status::Ok);

    std::array<int16_t, sonic_codec::kRxSamplesPerBlock> partial{};
    std::copy(pcm.begin(), pcm.begin() + static_cast<std::ptrdiff_t>(partial.size()), partial.begin());
    uint8_t discarded_frame[sonic_codec::kFrameBytes]{};
    assert(codec.feed_rx(partial.data(), partial.size(), discarded_frame) == sonic_codec::Status::NoFrame);
    assert(codec.reset_rx() == sonic_codec::Status::Ok);
    for (size_t i = 0; i < 8u; ++i) {
        std::array<int16_t, sonic_codec::kRxSamplesPerBlock> silence{};
        assert(codec.feed_rx(silence.data(), silence.size(), discarded_frame) ==
               sonic_codec::Status::NoFrame);
    }
    assert(codec.reset_rx() == sonic_codec::Status::Ok);

    uint8_t decoded[sonic_codec::kFrameBytes]{};
    bool got_frame = false;
    for (size_t offset = 0; offset < pcm.size(); offset += sonic_codec::kRxSamplesPerBlock) {
        std::array<int16_t, sonic_codec::kRxSamplesPerBlock> block{};
        const size_t available = pcm.size() - offset;
        const size_t count = available < block.size() ? available : block.size();
        std::copy(pcm.begin() + static_cast<std::ptrdiff_t>(offset),
                  pcm.begin() + static_cast<std::ptrdiff_t>(offset + count), block.begin());
        const sonic_codec::Status status = codec.feed_rx(block.data(), block.size(), decoded);
        if (status == sonic_codec::Status::Ok) {
            got_frame = true;
            break;
        }
        assert(status == sonic_codec::Status::NoFrame);
    }
    assert(got_frame);
    assert(std::memcmp(decoded, token->frames[0], sonic_codec::kFrameBytes) == 0);

    // Repeated mode changes reset the same prepared donor allocation.
    for (int i = 0; i < 3; ++i) {
        assert(codec.reset_rx() == sonic_codec::Status::Ok);
        assert(codec.prepare_tx(token->frames[0], sonic_codec::kFrameBytes, 75,
                                &tones, &protocol) == sonic_codec::Status::Ok);
        assert(codec.heap_bytes() == heap_bytes);
    }
    std::puts("test_sonic_codec: PASS");
    return 0;
}
