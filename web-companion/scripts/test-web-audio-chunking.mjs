import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import createModem from '../public/wasm/ggwave.js';

const wasmDir = new URL('../public/wasm/', import.meta.url);
const modemBinary = await readFile(new URL('ggwave.wasm', wasmDir));
const payload = Uint8Array.from({ length: 40 }, (_, index) => (index * 37 + (index % 3 ? 11 : 0)) & 0xff);

function createModule() {
  return createModem({ wasmBinary: modemBinary, locateFile: path => fileURLToPath(new URL(path, wasmDir)) });
}

async function encodeAt(sampleRate) {
  const modem = await createModule(); modem._sl_ggwave_quiet();
  const tx = modem._sl_ggwave_create_tx(sampleRate);
  assert.ok(tx >= 0, `TX instance initializes at ${sampleRate} Hz`);
  const frame = modem._malloc(payload.length); const outSlot = modem._malloc(4);
  modem.HEAPU8.set(payload, frame);
  const sampleCount = modem._sl_ggwave_encode(tx, frame, outSlot);
  assert.ok(sampleCount > 0, `40-byte waveform encodes at ${sampleRate} Hz`);
  const samplePtr = modem.HEAPU32[outSlot >>> 2];
  const samples = modem.HEAP16.slice(samplePtr >>> 1, (samplePtr >>> 1) + sampleCount);
  modem._sl_ggwave_free_samples(samplePtr); modem._sl_ggwave_free(tx); modem._free(frame); modem._free(outSlot);
  return samples;
}

async function decodeAt(sampleRate, input, feedSizes) {
  const modem = await createModule(); modem._sl_ggwave_quiet();
  const rx = modem._sl_ggwave_create_rx(sampleRate);
  assert.ok(rx >= 0, `RX instance initializes at ${sampleRate} Hz`);
  const inPtr = modem._malloc(512 * 2); const outPtr = modem._malloc(256);
  let pending = new Int16Array(0), decoded;
  let offset = 0, feed = 0;
  while (offset < input.length && !decoded) {
    const size = feedSizes[feed++ % feedSizes.length];
    const part = input.subarray(offset, Math.min(input.length, offset + size)); offset += part.length;
    const joined = new Int16Array(pending.length + part.length); joined.set(pending); joined.set(part, pending.length);
    let cursor = 0;
    while (cursor + 512 <= joined.length && !decoded) {
      modem.HEAP16.set(joined.subarray(cursor, cursor + 512), inPtr >>> 1);
      const count = modem._sl_ggwave_decode(rx, inPtr, 512, outPtr, 256);
      if (count === 40) decoded = modem.HEAPU8.slice(outPtr, outPtr + 40);
      cursor += 512;
    }
    pending = joined.slice(cursor);
  }
  modem._sl_ggwave_free(rx); modem._free(inPtr); modem._free(outPtr);
  return decoded;
}

async function resampleInputTo48k(inputRate, input) {
  const modem = await createModule(); modem._sl_ggwave_quiet();
  const resampler = modem._sl_ggwave_create_input_resampler(inputRate, 48000);
  assert.ok(resampler, `input resampler initializes for ${inputRate} Hz`);
  const collected = [];
  for (let offset = 0; offset + 64 < input.length; offset += 2048) {
    const part = input.subarray(offset, Math.min(input.length, offset + 2048));
    if (part.length <= 64) break;
    const inPtr = modem._malloc(part.length * 2);
    const capacity = Math.ceil(part.length * 48000 / inputRate) + 128;
    const outPtr = modem._malloc(capacity * 2);
    modem.HEAP16.set(part, inPtr >>> 1);
    const count = modem._sl_ggwave_resample_input(resampler, inPtr, part.length, outPtr, capacity);
    assert.ok(count > 0 && count <= capacity, `resamples ${part.length} input samples without clipping the output buffer`);
    collected.push(...modem.HEAP16.slice(outPtr >>> 1, (outPtr >>> 1) + count));
    modem._free(inPtr); modem._free(outPtr);
  }
  modem._sl_ggwave_free_input_resampler(resampler);
  return Int16Array.from(collected);
}

function transform(samples, gain = 1) {
  return Int16Array.from(samples, sample => Math.round(sample * gain));
}

for (const sampleRate of [44100, 48000]) {
  const encoded = await encodeAt(sampleRate);
  const envelope = new Int16Array(encoded.length + 4096); envelope.set(encoded, 2048);
  const boundaries = [1, 127, 509, 2049, 73, 1023, 17];
  const toDecoderRate = async samples => sampleRate === 48000 ? samples : resampleInputTo48k(sampleRate, samples);
  const recovered = await decodeAt(48000, await toDecoderRate(envelope), boundaries);
  assert.ok(recovered, `frame recovers at ${sampleRate} Hz across arbitrary input boundaries with leading/trailing silence`);
  assert.deepEqual(recovered, payload, `exact 40-byte frame recovers at ${sampleRate} Hz`);
  for (const gain of [0.25, 0.5, 1]) {
    const decoded = await decodeAt(48000, await toDecoderRate(transform(envelope, gain)), boundaries);
    assert.ok(decoded, `frame decodes at ${sampleRate} Hz and ${gain}x gain`);
    assert.deepEqual(decoded, payload, `gain ${gain}x preserves the exact frame at ${sampleRate} Hz`);
  }
  console.log(`test_web_audio_chunking: ${sampleRate} Hz, arbitrary boundaries, silence, 0.25x/0.5x/1.0x gain PASS`);
}

const encoded = await encodeAt(48000);
const clippingLimit = Math.max(1, Math.floor(Math.max(...encoded.map(sample => Math.abs(sample))) / 2));
const clipped = Int16Array.from(encoded, sample => Math.max(-clippingLimit, Math.min(clippingLimit, sample)));
const noisy = Int16Array.from(encoded, (sample, index) => Math.max(-32768, Math.min(32767, sample + ((index * 1103515245 + 12345) >>> 24) - 128)));
const corrupted = Int16Array.from(encoded, (sample, index) => index >= encoded.length / 3 && index < encoded.length / 2 ? 0 : sample);
assert.ok(clipped.some((sample, index) => sample !== encoded[index]), 'controlled clipping changes the fixture samples');
assert.ok(noisy.some((sample, index) => sample !== encoded[index]), 'deterministic white noise changes the fixture samples');
assert.ok(corrupted.some((sample, index) => sample !== encoded[index]), 'the corrupted region changes the fixture samples');
const destructive = [
  ['controlled clipping', clipped],
  ['deterministic white noise', noisy],
  ['corrupted region', corrupted],
];
for (const [label, samples] of destructive) {
  const decoded = await decodeAt(48000, samples, [193, 701, 11, 997]);
  assert.ok(decoded === undefined || Buffer.from(decoded).equals(Buffer.from(payload)), `${label} must either fail to decode or preserve the exact CRC-protected 40-byte frame`);
  console.log(`test_web_audio_chunking: ${label} degradation bounded (${decoded ? 'exact frame' : 'no frame'})`);
}
