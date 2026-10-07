<p align="right">
  <a href="sonic-memory-baseline.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Sonic Link Phase 8 Memory Baseline

## Decision

Keep the pinned stock ggwave donor and the fixed 40-byte frame. No allocator
patch was applied because a real Passport stock-memory failure has not been
observed. G18 remains **NOT RUN** until the thresholds below are measured on a
physical Passport. Simulator and host values are informational only.

No Passport serial device was present during this run. The available serial
ports were the macOS debug console and Bluetooth incoming port. The existing
Passport Simulator booted the Phase-8 image into Sonic Link, but its UART view
did not expose a `SONIC_MEM` record, so no simulator heap values are reported.

## Instrumentation

The audio engine retains the selected candidate, donor heap requirement,
largest free block sampled immediately before codec initialization, the
24 KiB reserve threshold, and whether that preflight passed. It preserves these
values if codec initialization fails. The application emits a structured
`SONIC_MEM` line after the first audio diagnostics snapshot, including runtime
free heap, minimum free heap, largest block, and codec heap.

The host threshold evaluator reports arithmetic only; it does not establish
the physical source of measurements or claim G18 acceptance. Thresholds are:

- Pre-codec largest block: donor required heap + 24,576 bytes.
- Runtime free heap: at least 49,152 bytes.
- Runtime minimum free heap: at least 32,768 bytes.
- Runtime largest block: at least 24,576 bytes.

For the current stock `AUDIBLE_FASTEST` candidate, donor required heap is
99,824 bytes, so the pre-codec largest block threshold is 124,400 bytes.

## Phase-8 Firmware Identity

- Target: ESP32-C3; ESP-IDF: v5.5.3.
- Source base: `33d3d1d93a1125b356b47b6d83a7a60121be801e` plus uncommitted
  Phase-8 measurement changes.
- Sonic semantic version: 1.0.0.
- Acoustic candidate: `AUDIBLE_FASTEST` (candidate only; not a release selection).
- Donor: pinned stock ggwave 0.4.3; no donor source changes.
- App binary: 810,432 bytes; merged full image: 875,968 bytes.
- Full-image archive: `build/firmware/7a102ad21dbf3599887683459e571fd593ab11b70f7d360ae9202028dacceeed`.
- Full-image SHA-256: `7a102ad21dbf3599887683459e571fd593ab11b70f7d360ae9202028dacceeed`.
- ELF SHA-256: `309b9f206ae4b1960cf6351017d1dde1bed1b4926e9777c23c971fa1d0114087`.
- DEVICE_INFO fingerprint bytes: `30 9B 9F 20`; displayed as `309B9F20`.

## Gate Status

- Firmware build and layout validation: PASS.
- Host memory threshold boundaries and retained preflight diagnostics: PASS.
- G05/G06 WASM regression: PASS.
- Existing simulator boot into Sonic Link: PASS.
- G18 physical memory gate: NOT RUN; no real Passport was available.
- No decision to patch the allocator or reduce frame size was made.
