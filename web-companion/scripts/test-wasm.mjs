import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import createCore from '../public/wasm/sonic-core.js';
import createModem from '../public/wasm/ggwave.js';

const wasmDir = new URL('../public/wasm/', import.meta.url);
const [coreBinary, modemBinary] = await Promise.all([
  readFile(new URL('sonic-core.wasm', wasmDir)), readFile(new URL('ggwave.wasm', wasmDir)),
]);
const core = await createCore({ wasmBinary: coreBinary, locateFile: path => fileURLToPath(new URL(path, wasmDir)) });
const modem = await createModem({ wasmBinary: modemBinary, locateFile: path => fileURLToPath(new URL(path, wasmDir)) });
modem._sl_ggwave_quiet();

const payload = Uint8Array.from([0x00, 0x80, 0xff, 0x31, 0x42, 0x43]);
const payloadPtr = core._malloc(payload.length); const framePtr = core._malloc(120);
core.HEAPU8.set(payload, payloadPtr);
assert.equal(core._sl_validate_payload(3, payloadPtr, payload.length), 0, 'TOKEN payload validates in production sonic_core');
assert.equal(core._sl_fragment(3, 0x5a3c, payloadPtr, payload.length, framePtr), 1, 'one binary frame is produced');
const expected = core.HEAPU8.slice(framePtr, framePtr + 40);
core._free(payloadPtr); core._free(framePtr);

const emptyPtr = core._malloc(1); const emptyFramePtr = core._malloc(120);
assert.equal(core._sl_validate_payload(3, emptyPtr, 0), 0, 'empty TOKEN is valid in production sonic_core');
assert.equal(core._sl_fragment(3, 0x5a3d, emptyPtr, 0, emptyFramePtr), 1, 'empty TOKEN produces one valid frame');
core._free(emptyPtr); core._free(emptyFramePtr);

const partialPayload = Uint8Array.from({ length: 40 }, (_, index) => index);
const partialPtr = core._malloc(partialPayload.length); const partialFramesPtr = core._malloc(120);
core.HEAPU8.set(partialPayload, partialPtr);
assert.equal(core._sl_fragment(3, 0x5a3e, partialPtr, partialPayload.length, partialFramesPtr), 2);
const partialFrames = [core.HEAPU8.slice(partialFramesPtr, partialFramesPtr + 40), core.HEAPU8.slice(partialFramesPtr + 40, partialFramesPtr + 80)];
core._free(partialPtr); core._free(partialFramesPtr);
const assembly = core._sl_reassembly_create(); const assemblyInput = core._malloc(40); const assemblyOut = core._malloc(101);
core.HEAPU8.set(partialFrames[0], assemblyInput);
assert.equal(core._sl_reassembly_accept(assembly, assemblyInput, 1000, assemblyOut), 1);
assert.equal(core._sl_reassembly_poll(assembly, 10999, assemblyOut), 1, 'partial is retained before timeout');
assert.equal(core._sl_reassembly_poll(assembly, 11000, assemblyOut), 4, 'core emits INCOMPLETE at exactly T0+10s');
assert.equal(core.HEAPU8[assemblyOut + 5], 1); assert.equal(core.HEAPU8[assemblyOut + 6], 2);
assert.equal(core._sl_reassembly_poll(assembly, 30999, assemblyOut), 1, 'partial is retained until hard expiry');
assert.equal(core._sl_reassembly_poll(assembly, 31000, assemblyOut), 7, 'core expires at exactly T0+30s');
assert.equal(core.HEAPU8[assemblyOut + 5], 1); assert.equal(core.HEAPU8[assemblyOut + 6], 2);
core.HEAPU8.set(partialFrames[0], assemblyInput);
assert.equal(core._sl_reassembly_accept(assembly, assemblyInput, 40000, assemblyOut), 1);
assert.equal(core._sl_reassembly_poll(assembly, 50000, assemblyOut), 4);
core.HEAPU8.set(partialFrames[1], assemblyInput);
assert.equal(core._sl_reassembly_accept(assembly, assemblyInput, 50001, assemblyOut), 5, 'same-ID Listen Again can complete retained partial assembly');
assert.deepEqual(core.HEAPU8.slice(assemblyOut + 8, assemblyOut + 48), partialPayload);
core._sl_reassembly_free(assembly); core._free(assemblyInput); core._free(assemblyOut);

const tx = modem._sl_ggwave_create_tx(48000);
const rx = modem._sl_ggwave_create_rx(48000);
assert.ok(tx >= 0 && rx >= 0, 'TX/RX use the actual 48 kHz AudioContext sample rate');
const input = modem._malloc(40); const ptrSlot = modem._malloc(4);
modem.HEAPU8.set(expected, input);
const sampleCount = modem._sl_ggwave_encode(tx, input, ptrSlot);
assert.ok(sampleCount > 0, `pinned AUDIBLE_FASTEST candidate creates a waveform (${sampleCount})`);
const samplePtr = modem.HEAPU32[ptrSlot >>> 2];
const samples = modem.HEAP16.slice(samplePtr >>> 1, (samplePtr >>> 1) + sampleCount);
modem._sl_ggwave_free_samples(samplePtr);
const pcmPtr = modem._malloc(512 * 2); const outPtr = modem._malloc(256);
let decoded = false;
for (let offset = 0; offset < samples.length; offset += 512) {
  const part = samples.subarray(offset, Math.min(offset + 512, samples.length));
  if (part.length !== 512) break;
  modem.HEAP16.set(part, pcmPtr >>> 1);
  const result = modem._sl_ggwave_decode(rx, pcmPtr, 512, outPtr, 256);
  if (result === 40) {
    decoded = modem.HEAPU8.slice(outPtr, outPtr + 40).every((byte, i) => byte === expected[i]);
    break;
  }
}
assert.equal(decoded, true, 'pinned stock ggwave WASM recovers the exact binary frame');
modem._sl_ggwave_free(tx); modem._sl_ggwave_free(rx);
modem._free(input); modem._free(ptrSlot); modem._free(pcmPtr); modem._free(outPtr);
console.log('Web WASM smoke: sonic_core binary frame + pinned ggwave 40-byte encode/decode PASS');
