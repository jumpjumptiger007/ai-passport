import { describe, expect, it } from 'vitest';
import { takeSampleBlocks } from '../src/audio/sample-chunker';
import { OneShotDebugCapture } from '../src/audio/debug-capture';

describe('test_web_audio_chunking', () => {
  it('emits fixed 512-sample modem blocks independent of incoming chunk sizes', () => {
    let remainder = new Int16Array(0); let total = 0;
    for (const size of [128, 1024, 300, 2048, 17, 1531]) {
      const result = takeSampleBlocks(remainder, new Int16Array(size));
      total += result.blocks.reduce((sum, block) => sum + block.length, 0);
      remainder = result.remainder;
      expect(remainder.length).toBeLessThan(512);
    }
    expect(total + remainder.length).toBe(5048);
    expect(total % 512).toBe(0);
  });

  it('preserves sample values through arbitrary boundaries including leading and trailing silence', () => {
    const source = Int16Array.from({ length: 9001 }, (_, index) => index < 17 || index >= 8990 ? 0 : ((index * 977) & 0xffff) - 32768);
    const sizes = [1, 127, 509, 2049, 73, 1023, 17];
    const output: number[] = [];
    let remainder = new Int16Array(0), offset = 0, index = 0;
    while (offset < source.length) {
      const part = source.subarray(offset, offset + sizes[index++ % sizes.length]); offset += part.length;
      const result = takeSampleBlocks(remainder, part);
      output.push(...result.blocks.flatMap(block => [...block]));
      remainder = result.remainder;
    }
    output.push(...remainder);
    expect(output).toEqual([...source]);
    expect(output.slice(0, 17).every(sample => sample === 0)).toBe(true);
    expect(output.slice(-11).every(sample => sample === 0)).toBe(true);
  });
});

describe('one-shot debug capture', () => {
  it('is off by default, bounded to 30 seconds, and clears memory', () => {
    const capture = new OneShotDebugCapture();
    expect(capture.isArmed).toBe(false);
    capture.arm(48000);
    expect(capture.push(new Int16Array(48000))).toBe(true);
    expect(capture.sampleCount).toBe(48000);
    capture.push(new Int16Array(48000 * 30));
    expect(capture.sampleCount).toBe(48000 * 30);
    expect(capture.isArmed).toBe(false);
    expect(capture.toWav().size).toBe(44 + 48000 * 30 * 2);
    capture.clear();
    expect(capture.sampleCount).toBe(0);
  });
  it('keeps an early finished recording available for download', () => {
    const capture = new OneShotDebugCapture(); capture.arm(24000); capture.push(new Int16Array(240)); capture.finish();
    expect(capture.sampleCount).toBe(240);
    expect(capture.toWav().size).toBe(524);
  });
});
