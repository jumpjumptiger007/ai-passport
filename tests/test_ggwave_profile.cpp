#include <cassert>
#include <cstdio>

#include "sonic_ggwave_profile.h"

namespace {

using sonic_ggwave_profile::Candidate;

void assert_only_protocol(const GGWave::Protocols &protocols, GGWave::ProtocolId selected) {
    for (int i = 0; i < GGWAVE_PROTOCOL_COUNT; ++i) {
        assert(protocols[i].enabled == (i == static_cast<int>(selected)));
    }
}

void assert_candidate(Candidate candidate, GGWave::ProtocolId expected_protocol, int expected_heap_bytes) {
    GGWave::ProtocolId mapped_protocol = GGWAVE_PROTOCOL_COUNT;
    assert(sonic_ggwave_profile::protocol_id(candidate, &mapped_protocol));
    assert(mapped_protocol == expected_protocol);

    const GGWave::Parameters parameters = sonic_ggwave_profile::passport_parameters();
    assert(parameters.payloadLength == 40);
    assert(parameters.sampleRateInp == 24000.0f);
    assert(parameters.sampleRateOut == 24000.0f);
    assert(parameters.sampleRate == 24000.0f);
    assert(parameters.samplesPerFrame == 512);
    assert(parameters.sampleFormatInp == GGWAVE_SAMPLE_FORMAT_I16);
    assert(parameters.sampleFormatOut == GGWAVE_SAMPLE_FORMAT_I16);
    assert((parameters.operatingMode & GGWAVE_OPERATING_MODE_RX) != 0);
    assert((parameters.operatingMode & GGWAVE_OPERATING_MODE_TX) != 0);
    assert((parameters.operatingMode & GGWAVE_OPERATING_MODE_TX_ONLY_TONES) != 0);
    assert((parameters.operatingMode & GGWAVE_OPERATING_MODE_USE_DSS) != 0);

    int dry_run_heap_bytes = 0;
    assert(sonic_ggwave_profile::required_heap_bytes(candidate, &dry_run_heap_bytes));
    assert(dry_run_heap_bytes == expected_heap_bytes);

    GGWave instance;
    assert(sonic_ggwave_profile::prepare(instance, candidate));
    assert(instance.isDSSEnabled());
    assert(instance.samplesPerFrame() == 512);
    assert(instance.sampleRateInp() == 24000.0f);
    assert(instance.sampleRateOut() == 24000.0f);
    assert(instance.sampleFormatInp() == GGWAVE_SAMPLE_FORMAT_I16);
    assert(instance.sampleFormatOut() == GGWAVE_SAMPLE_FORMAT_I16);
    assert(instance.heapSize() == dry_run_heap_bytes);
    assert_only_protocol(instance.rxProtocols(), expected_protocol);
    assert_only_protocol(instance.txProtocols(), expected_protocol);
}

}  // namespace

int main() {
    GGWave::setLogFile(nullptr);

    assert_candidate(Candidate::AudibleFastest, GGWAVE_PROTOCOL_AUDIBLE_FASTEST, 99824);
    assert_candidate(Candidate::AudibleFast, GGWAVE_PROTOCOL_AUDIBLE_FAST, 185840);

    // Candidate reconfiguration must clear the previously selected protocol.
    assert(sonic_ggwave_profile::configure_protocols(Candidate::AudibleFastest));
    assert(sonic_ggwave_profile::configure_protocols(Candidate::AudibleFast));
    assert_only_protocol(GGWave::Protocols::rx(), GGWAVE_PROTOCOL_AUDIBLE_FAST);
    assert_only_protocol(GGWave::Protocols::tx(), GGWAVE_PROTOCOL_AUDIBLE_FAST);

    assert(!sonic_ggwave_profile::protocol_id(static_cast<Candidate>(99), nullptr));
    GGWave::ProtocolId invalid_protocol = GGWAVE_PROTOCOL_COUNT;
    assert(!sonic_ggwave_profile::protocol_id(static_cast<Candidate>(99), &invalid_protocol));
    assert(!sonic_ggwave_profile::configure_protocols(static_cast<Candidate>(99)));

    std::puts("test_ggwave_profile: PASS");
    return 0;
}
