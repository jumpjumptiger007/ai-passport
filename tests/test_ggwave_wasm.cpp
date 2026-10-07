#include <cstdio>
#include <cstring>
#include <vector>

#include "sonic_ggwave_profile.h"
#include "sonic_golden_vectors.h"

int main() {
    GGWave::setLogFile(nullptr);
    const sonic_golden_vector_t *token = nullptr;
    for (size_t i = 0; i < sonic_golden_vector_count; ++i) {
        if (std::strcmp(sonic_golden_vectors[i].name, "token_nul") == 0) {
            token = &sonic_golden_vectors[i];
            break;
        }
    }
    if (token == nullptr || token->frame_count != 1 || token->frames[0][8] != 0 ||
        token->frames[0][7] <= 0x7f || token->frames[0][39] <= 0x7f) {
        std::fprintf(stderr, "G06: invalid binary 40-byte Sonic TOKEN fixture\n");
        return 1;
    }

    const sonic_ggwave_profile::Candidate candidate = sonic_ggwave_profile::Candidate::AudibleFastest;
    if (!sonic_ggwave_profile::configure_protocols(candidate)) {
        std::fprintf(stderr, "G06: protocol selection failed\n");
        return 1;
    }
    GGWave::Parameters parameters = sonic_ggwave_profile::passport_parameters();
    parameters.operatingMode &= ~GGWAVE_OPERATING_MODE_TX_ONLY_TONES;

    GGWave transmitter;
    if (!transmitter.prepare(parameters, true)) {
        std::fprintf(stderr, "G06: transmitter prepare failed\n");
        return 1;
    }
    if (!transmitter.init(static_cast<int>(SONIC_FRAME_SIZE),
                          reinterpret_cast<const char *>(token->frames[0]),
                          GGWAVE_PROTOCOL_AUDIBLE_FASTEST, 50)) {
        std::fprintf(stderr, "G06: binary pointer-plus-length init failed\n");
        return 1;
    }
    const uint32_t waveform_bytes = transmitter.encode();
    if (waveform_bytes == 0 || waveform_bytes % sizeof(int16_t) != 0 ||
        transmitter.txWaveform() == nullptr) {
        std::fprintf(stderr, "G06: full waveform encode failed\n");
        return 1;
    }

    if (!sonic_ggwave_profile::configure_protocols(candidate)) {
        std::fprintf(stderr, "G06: receiver protocol selection failed\n");
        return 1;
    }
    parameters.operatingMode = GGWAVE_OPERATING_MODE_RX | GGWAVE_OPERATING_MODE_USE_DSS;
    GGWave receiver;
    if (!receiver.prepare(parameters, true)) {
        std::fprintf(stderr, "G06: receiver prepare failed\n");
        return 1;
    }

    const int16_t *samples = static_cast<const int16_t *>(transmitter.txWaveform());
    const size_t sample_count = waveform_bytes / sizeof(int16_t);
    bool decoded = false;
    for (size_t offset = 0; offset < sample_count; offset += 512) {
        const size_t count = (sample_count - offset < 512) ? sample_count - offset : 512;
        if (!receiver.decode(samples + offset, static_cast<uint32_t>(count * sizeof(int16_t)))) {
            std::fprintf(stderr, "G06: stock receiver rejected PCM block at %lu\n",
                         static_cast<unsigned long>(offset));
            return 1;
        }
        if (receiver.rxDataLength() == static_cast<int>(SONIC_FRAME_SIZE)) {
            decoded = std::memcmp(receiver.rxData().data(), token->frames[0], SONIC_FRAME_SIZE) == 0;
            break;
        }
    }
    if (!decoded) {
        std::fprintf(stderr, "G06: exact 40-byte decode failed (length=%d)\n", receiver.rxDataLength());
        return 1;
    }
    std::puts("G06: stock ggwave WASM binary 40-byte round-trip PASS (embedded NUL and high-bit bytes preserved)");
    return 0;
}
