#pragma once

#include <cstdint>

#include "ggwave/ggwave.h"

namespace sonic_ggwave_profile {

enum class Candidate : uint8_t {
    AudibleFastest = 0,
    AudibleFast = 1,
};

// Maps a frozen Sonic Link release candidate to its donor protocol ID.
bool protocol_id(Candidate candidate, GGWave::ProtocolId *out_protocol);

// Returns donor defaults with only the parameters frozen for Passport changed.
GGWave::Parameters passport_parameters();

// Replaces both donor-global protocol sets with only the selected candidate.
bool configure_protocols(Candidate candidate);

// Configures protocol sets and prepares one stock donor instance.
bool prepare(GGWave &instance, Candidate candidate, bool allocate = true);

// Uses stock GGWave::prepare(parameters, false) to calculate the required heap.
bool required_heap_bytes(Candidate candidate, int *out_bytes);

}  // namespace sonic_ggwave_profile
