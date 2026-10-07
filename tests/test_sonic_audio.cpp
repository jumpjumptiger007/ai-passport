#include <array>
#include <cassert>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "sonic_audio_engine.h"
#include "sonic_golden_vectors.h"

namespace {

struct MockIo {
    bool configure_ok = true;
    bool read_ok = true;
    bool write_ok = true;
    bool volume_ok = true;
    size_t largest_block = 256u * 1024u;
    uint32_t configured_hz = 0;
    uint8_t configured_bits = 0;
    uint8_t configured_channels = 0;
    uint8_t volume_percent = 0;
    size_t read_calls = 0;
    size_t write_calls = 0;
    uint64_t clock_us = 0;
    std::vector<int16_t> written;
};

bool mock_configure(void *context, uint32_t hz, uint8_t bits, uint8_t channels) {
    auto &io = *static_cast<MockIo *>(context);
    io.configured_hz = hz;
    io.configured_bits = bits;
    io.configured_channels = channels;
    return io.configure_ok;
}

size_t mock_largest_block(void *context) {
    return static_cast<MockIo *>(context)->largest_block;
}

bool mock_set_volume(void *context, uint8_t percent) {
    auto &io = *static_cast<MockIo *>(context);
    io.volume_percent = percent;
    return io.volume_ok;
}

bool mock_read(void *context, int16_t *samples, size_t count) {
    auto &io = *static_cast<MockIo *>(context);
    ++io.read_calls;
    if (!io.read_ok) return false;
    std::fill(samples, samples + count, static_cast<int16_t>(16384));
    return true;
}

bool mock_write(void *context, const int16_t *samples, size_t count) {
    auto &io = *static_cast<MockIo *>(context);
    ++io.write_calls;
    if (!io.write_ok) return false;
    io.written.insert(io.written.end(), samples, samples + count);
    return true;
}

uint64_t mock_now_us(void *context) {
    auto &io = *static_cast<MockIo *>(context);
    io.clock_us += 100u;
    return io.clock_us;
}

sonic_audio::Operations operations(MockIo *io) {
    sonic_audio::Operations result;
    result.context = io;
    result.configure = mock_configure;
    result.set_volume = mock_set_volume;
    result.largest_free_block = mock_largest_block;
    result.read = mock_read;
    result.write = mock_write;
    result.now_us = mock_now_us;
    return result;
}

const sonic_golden_vector_t *find_vector(const char *name) {
    for (size_t i = 0; i < sonic_golden_vector_count; ++i) {
        if (std::strcmp(sonic_golden_vectors[i].name, name) == 0) {
            return &sonic_golden_vectors[i];
        }
    }
    return nullptr;
}

void test_config_and_rx() {
    MockIo io;
    sonic_audio::Engine engine;
    assert(engine.initialize(operations(&io)) == SONIC_OK);
    assert(io.configured_hz == 24000u && io.configured_bits == 16u && io.configured_channels == 1u);
    assert(io.volume_percent == 0u);
    assert(engine.state() == sonic_audio::State::Idle);
    const auto initialized_diagnostics = engine.diagnostics();
    assert(initialized_diagnostics.codec_preflight_measured);
    assert(initialized_diagnostics.codec_preflight_passed);
    assert(initialized_diagnostics.codec_required_heap_bytes ==
           static_cast<size_t>(sonic_codec::Codec::required_heap_bytes(
               sonic_ggwave_profile::Candidate::AudibleFastest)));
    assert(initialized_diagnostics.codec_required_with_reserve_bytes ==
           initialized_diagnostics.codec_required_heap_bytes +
               sonic_codec::kMinimumFreeHeapReserveBytes);
    assert(initialized_diagnostics.pre_codec_largest_free_block_bytes == io.largest_block);
    assert(engine.start_rx().error == SONIC_OK);
    const auto read_event = engine.read_rx_once();
    assert(read_event.type == sonic_audio::EventType::None);
    assert(io.read_calls == 1u);
    auto diagnostics = engine.diagnostics();
    assert(diagnostics.microphone_rms == 16384.0f);
    assert(diagnostics.microphone_dbfs < 0.0f && diagnostics.microphone_dbfs > -7.0f);
    assert(diagnostics.rx_processing_count == 1u);
    assert(diagnostics.rx_processing_average_us == 100u);
    assert(diagnostics.rx_processing_p99_us == 100u);
    engine.set_worker_stack_high_water(1536u);
    assert(engine.diagnostics().worker_stack_high_water_words == 1536u);
    assert(engine.stop_rx().error == SONIC_OK);
}

void test_frame_receive_and_tx_guards() {
    const auto *frames = find_vector("max_ascii_text");
    assert(frames != nullptr && frames->frame_count == 3u);
    MockIo io;
    sonic_audio::Engine engine;
    assert(engine.initialize(operations(&io)) == SONIC_OK);
    assert(engine.start_rx().error == SONIC_OK);

    // Exercise the engine's physical-frame event with the accepted renderer.
    assert(sonic_ggwave_profile::configure_protocols(
        sonic_ggwave_profile::Candidate::AudibleFastest));
    GGWave tx;
    assert(sonic_ggwave_profile::prepare(tx, sonic_ggwave_profile::Candidate::AudibleFastest));
    GGWave::ProtocolId id = GGWAVE_PROTOCOL_COUNT;
    assert(sonic_ggwave_profile::protocol_id(sonic_ggwave_profile::Candidate::AudibleFastest, &id));
    assert(tx.init(sonic_codec::kFrameBytes, reinterpret_cast<const char *>(frames->frames[0]), id, 75));
    (void) tx.encode();
    const GGWave::Tones tones = tx.txTones();
    const GGWave::Protocol protocol = tx.txProtocols()[id];
    sonic_tone_renderer::Renderer renderer;
    assert(sonic_tone_renderer::begin(&renderer, tones, protocol, 24000.0, 512, 75));
    std::array<int16_t, 256> chunk{};
    std::vector<int16_t> pcm;
    for (;;) {
        size_t written = 0u;
        const auto status = sonic_tone_renderer::render_next(&renderer, chunk.data(), chunk.size(),
                                                              &written, false);
        pcm.insert(pcm.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(written));
        if (status == sonic_tone_renderer::Status::Complete) break;
        assert(status == sonic_tone_renderer::Status::ChunkReady);
    }
    sonic_audio::Event frame_event;
    for (size_t offset = 0; offset < pcm.size(); offset += sonic_audio::kSamplesPerBlock) {
        std::array<int16_t, sonic_audio::kSamplesPerBlock> block{};
        const size_t count = std::min(block.size(), pcm.size() - offset);
        std::copy(pcm.begin() + static_cast<std::ptrdiff_t>(offset),
                  pcm.begin() + static_cast<std::ptrdiff_t>(offset + count), block.begin());
        frame_event = engine.process_rx_block(block.data(), block.size());
        if (frame_event.type == sonic_audio::EventType::FrameReceived) break;
        assert(frame_event.type == sonic_audio::EventType::None);
    }
    assert(frame_event.type == sonic_audio::EventType::FrameReceived);
    assert(std::memcmp(frame_event.frame, frames->frames[0], sonic_codec::kFrameBytes) == 0);

    const size_t read_calls_before_tx = io.read_calls;
    assert(engine.begin_tx(frames->frames, 2u, 75, true).type == sonic_audio::EventType::TxProgress);
    assert(io.volume_percent == 75u);
    assert(engine.state() == sonic_audio::State::Tx);
    assert(engine.read_rx_once().error == SONIC_INVALID_ARGUMENT);
    for (size_t steps = 0; engine.state() == sonic_audio::State::Tx ||
                           engine.state() == sonic_audio::State::Stopping; ++steps) {
        assert(steps < 2000u);
        (void) engine.tx_step(false);
    }
    assert(io.read_calls == read_calls_before_tx);
    assert(io.write_calls > 0u);
    auto diagnostics = engine.diagnostics();
    assert(diagnostics.state == sonic_audio::State::Rx);
    assert(diagnostics.tx_inter_fragment_silence_samples == sonic_audio::kGuardSamples);
    assert(diagnostics.tx_guard_silence_samples == sonic_audio::kGuardSamples);
    assert(diagnostics.tx_frame_count == 2u && diagnostics.tx_frame_index == 1u);
    assert(diagnostics.speaker_volume_percent == 75u);
    assert(diagnostics.tx_elapsed_us > 0u);
    assert(engine.read_rx_once().type == sonic_audio::EventType::None);

    assert(engine.begin_tx(frames->frames, 2u, 75, true).type == sonic_audio::EventType::TxProgress);
    const auto first_chunk = engine.tx_step(false);
    assert(first_chunk.type == sonic_audio::EventType::TxProgress);
    const size_t reads_while_tx = io.read_calls;
    (void) engine.tx_step(true);
    sonic_audio::Event cancel_event;
    for (size_t steps = 0; engine.state() == sonic_audio::State::Tx ||
                           engine.state() == sonic_audio::State::Stopping; ++steps) {
        assert(steps < 100u);
        cancel_event = engine.tx_step(false);
    }
    assert(cancel_event.type == sonic_audio::EventType::TxCancelled);
    assert(io.read_calls == reads_while_tx);
    diagnostics = engine.diagnostics();
    assert(diagnostics.state == sonic_audio::State::Rx);
    assert(diagnostics.tx_guard_silence_samples == sonic_audio::kGuardSamples);
}

void test_failures() {
    MockIo init_io;
    init_io.configure_ok = false;
    sonic_audio::Engine init_engine;
    assert(init_engine.initialize(operations(&init_io)) == SONIC_AUDIO_INIT_FAILED);
    assert(init_engine.state() == sonic_audio::State::Error);

    MockIo memory_io;
    memory_io.largest_block = 1000u;
    sonic_audio::Engine memory_engine;
    assert(memory_engine.initialize(operations(&memory_io)) == SONIC_AUDIO_INIT_FAILED);
    const auto failed_memory_diagnostics = memory_engine.diagnostics();
    assert(failed_memory_diagnostics.codec_preflight_measured);
    assert(!failed_memory_diagnostics.codec_preflight_passed);
    assert(failed_memory_diagnostics.pre_codec_largest_free_block_bytes == 1000u);
    assert(failed_memory_diagnostics.codec_heap_bytes == 0);

    MockIo read_io;
    read_io.read_ok = false;
    sonic_audio::Engine read_engine;
    assert(read_engine.initialize(operations(&read_io)) == SONIC_OK);
    assert(read_engine.start_rx().error == SONIC_OK);
    assert(read_engine.read_rx_once().error == SONIC_AUDIO_READ_FAILED);
    assert(read_engine.state() == sonic_audio::State::Error);

    const auto *single = find_vector("text_a");
    assert(single != nullptr);
    uint8_t frames[sonic_audio::kTxFrameLimit][sonic_codec::kFrameBytes]{};
    std::memcpy(frames[0], single->frames[0], sonic_codec::kFrameBytes);
    MockIo write_io;
    write_io.write_ok = false;
    sonic_audio::Engine write_engine;
    assert(write_engine.initialize(operations(&write_io)) == SONIC_OK);
    assert(write_engine.begin_tx(frames, 1u, 75, false).error == SONIC_OK);
    assert(write_engine.tx_step(false).error == SONIC_AUDIO_WRITE_FAILED);
    assert(write_engine.state() == sonic_audio::State::Error);
}

}  // namespace

int main() {
    GGWave::setLogFile(nullptr);
    test_config_and_rx();
    test_frame_receive_and_tx_guards();
    test_failures();
    std::puts("test_sonic_audio: PASS");
    return 0;
}
