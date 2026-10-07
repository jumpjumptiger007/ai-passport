<p align="right">
  <a href="sonic-ggwave-profile.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Sonic Link fixed ggwave profile

## Scope and pin

This host/source proof uses the stock `ggerganov/ggwave` donor at commit
`060aec73dd7123ccac200442f75bdc7369795ffe`. Donor source is unchanged. It
does not select a shipping acoustic profile and does not establish Passport
runtime memory or acoustic performance.

The product-side profile builder is
`components/sonic_ggwave_profile/sonic_ggwave_profile.cpp`. It starts from
donor defaults, then fixes the Passport values required by Sonic Link:

| Parameter | Value |
|---|---:|
| Payload length | 40 bytes, fixed mode |
| Input/output/operating sample rate | 24,000 Hz |
| Samples per frame | 512 |
| Input/output sample format | I16 |
| Operating mode | RX + TX + TX_ONLY_TONES + DSS |

Before preparing an instance, the builder resets both donor-global protocol
sets and enables exactly one candidate in each direction. The candidates are
tested independently:

| Candidate | RX and TX protocol | Stock host heap |
|---|---|---:|
| AUDIBLE_FASTEST | `GGWAVE_PROTOCOL_AUDIBLE_FASTEST` | 99,824 bytes |
| AUDIBLE_FAST | `GGWAVE_PROTOCOL_AUDIBLE_FAST` | 185,840 bytes |

These exact values are regression expectations from the pinned donor's
`GGWave::prepare(parameters, false)` calculation. The test also allocates a
stock instance and checks that its `heapSize()` matches the dry run. The two
profiles differ because each protocol uses a different number of frames per
transmission, which changes the fixed RX history and tone-plan buffers.

## Allocation model

The donor computes these values in `ggwave_vendor/src/ggwave.cpp`, in
`GGWave::alloc`:

- common encoded payload and ECC data;
- fixed RX FFT, input, decoded-data, detected-bin/tone buffers;
- `spectrumHistoryFixed`, sized from `totalTxs * maxFramesPerTx * samplesPerFrame`;
- TX data, bits, and tone-plan buffers;
- Reed-Solomon work data.

Fixed RX history uses the donor's `totalTxs` calculation, whose helper counts
the selected profile's bytes-per-transmission in a way that can over-allocate
the history. This phase records stock behavior only. It does not patch the
allocator. Variable-length RX is not enabled, and TX_ONLY_TONES avoids the
donor's full-waveform TX buffers.

The byte counts above are host/source evidence for the pinned stock donor.
They are not Passport free-heap or largest-block measurements. Gate G18 on a
real Passport remains authoritative; an allocator-sizing-only patch is only
considered if those measurements demonstrate the specified memory failure.

## Validation

`tests/test_ggwave_profile.cpp` verifies the parameter contract, candidate
mapping, protocol isolation in both directions, DSS and instance properties,
dry-run versus allocated heap size, exact pinned heap regressions, and
reconfiguration from FASTEST to FAST. The test runs from
`./tools/validate.sh --static`.
