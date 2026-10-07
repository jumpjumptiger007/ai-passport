<p align="right">
  <strong>English</strong> · <a href="sonic-web-companion.zh_CN.md">简体中文</a>
</p>

# Sonic Link Web Companion

Phase 9 adds a static, same-origin mobile PWA in `web-companion/`. It has no
backend, account, telemetry, analytics, remote font, CDN, or upload path.
The browser and AI Passport exchange the existing v1 binary 40-byte frames.

## Local build and checks

Use Node.js 20 or newer, npm, and an Emscripten SDK available on `PATH` (or set
`EMCC` and `EMXX`). From `web-companion/`:

```sh
npm ci
npm run typecheck
npm run build
npm test
npm run test:wasm
npm run test:browser
```

`npm run build` compiles production `components/sonic_core/sonic_core.c` and
the pinned stock ggwave donor into local WASM, then builds the PWA and a
versioned service-worker precache. Build output is in the ignored `dist/` and
generated WASM in ignored `public/wasm/`. The browser test uses a local
production preview and requires the matching Playwright Chromium browser.

For an explicit SDK path:

```sh
EMCC=/path/to/emsdk/upstream/emscripten/emcc \
EMXX=/path/to/emsdk/upstream/emscripten/em++ npm run build
```

## Runtime architecture

The C ABI in `src/wasm/sonic-core-wrapper.c` calls the production Sonic Core
for payload validation, frame encoding, and receive reassembly. The C++ ABI
in `src/wasm/ggwave-web-wrapper.cpp` calls the pinned donor using the shared
AUDIBLE_FASTEST candidate configuration. Frames remain exactly 40 bytes and
RX preserves binary bytes.

Receive is explicitly started by the user. The browser requests microphone
capture with echo cancellation, automatic gain control, and noise suppression
disabled. AudioWorklet converts arbitrary browser render quanta into bounded
PCM chunks; a Worker aligns them to donor blocks and owns continuous decode and
Sonic Core reassembly. The AudioContext's actual input sample rate is passed to
the pinned donor resampler; converted 48 kHz blocks enter the modem receiver.
The UI displays both the AudioContext and observed microphone-track rates.
The worklet derives throttled RMS input levels from microphone samples for the
receive signal bars. TX shows only real frame/playback progress.
Receive audio is processed locally and not retained in normal operation.

Transmit progress comes from actual AudioBufferSource completion for each
encoded frame, with the protocol gap between frames. The final state is
complete only after the last source ends. Cancellation and page hiding are
interrupted states. Completion does not acknowledge device reception.

The optional debug capture is off by default, one-shot, capped at 30 seconds
at the observed AudioContext rate, held in memory, and exported only as a
user-requested WAV download. It is not included in the service-worker cache.

Web TEXT normalizes CRLF and CR to LF and accepts printable ASCII plus LF.
TOKEN accepts arbitrary binary content from 0 through 93 bytes. Each build
derives a 12-character identity from the production assets; it is exposed in
`version.json` and Diagnostics and included in the service-worker cache name.
`npm run test:wasm` checks real ggwave frame recovery at 44.1 and 48 kHz,
including arbitrary decode boundaries, silence, gain changes, and bounded
degradation under clipping/noise/corruption. State-machine coverage is in
`test_web_state`.

## Release state

The profile remains **AUDIBLE_FASTEST candidate**; this UI does not select a
shipping profile. Phase 9 does not deploy the PWA or pair a web artifact with a
firmware release. Physical Safari/Chrome acoustic acceptance and all required
Passport hardware gates remain NOT RUN until performed on the specified devices.
