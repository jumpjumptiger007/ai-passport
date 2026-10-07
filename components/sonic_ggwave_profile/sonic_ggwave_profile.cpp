#include "sonic_ggwave_profile.h"

namespace sonic_ggwave_profile {

bool protocol_id(Candidate candidate, GGWave::ProtocolId *out_protocol) {
    if (out_protocol == nullptr) {
        return false;
    }

    switch (candidate) {
        case Candidate::AudibleFastest:
            *out_protocol = GGWAVE_PROTOCOL_AUDIBLE_FASTEST;
            return true;
        case Candidate::AudibleFast:
            *out_protocol = GGWAVE_PROTOCOL_AUDIBLE_FAST;
            return true;
        default:
            return false;
    }
}

GGWave::Parameters passport_parameters() {
    GGWave::Parameters parameters = GGWave::getDefaultParameters();
    parameters.payloadLength = 40;
    parameters.sampleRateInp = 24000.0f;
    parameters.sampleRateOut = 24000.0f;
    parameters.sampleRate = 24000.0f;
    parameters.samplesPerFrame = 512;
    parameters.sampleFormatInp = GGWAVE_SAMPLE_FORMAT_I16;
    parameters.sampleFormatOut = GGWAVE_SAMPLE_FORMAT_I16;
    parameters.operatingMode = GGWAVE_OPERATING_MODE_RX |
                               GGWAVE_OPERATING_MODE_TX |
                               GGWAVE_OPERATING_MODE_TX_ONLY_TONES |
                               GGWAVE_OPERATING_MODE_USE_DSS;
    return parameters;
}

bool configure_protocols(Candidate candidate) {
    GGWave::ProtocolId id;
    if (!protocol_id(candidate, &id)) {
        return false;
    }

    GGWave::Protocols::rx().only(id);
    GGWave::Protocols::tx().only(id);
    return true;
}

bool prepare(GGWave &instance, Candidate candidate, bool allocate) {
    if (!configure_protocols(candidate)) {
        return false;
    }
    return instance.prepare(passport_parameters(), allocate);
}

bool required_heap_bytes(Candidate candidate, int *out_bytes) {
    if (out_bytes == nullptr || !configure_protocols(candidate)) {
        return false;
    }

    GGWave instance;
    if (!instance.prepare(passport_parameters(), false)) {
        return false;
    }
    *out_bytes = instance.heapSize();
    return *out_bytes > 0;
}

}  // namespace sonic_ggwave_profile
