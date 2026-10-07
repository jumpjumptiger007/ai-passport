import { describe, expect, it } from 'vitest';
import { estimatedSeconds, frameCount, parseToken, prepareText, prepareUrl } from '../src/protocol/payload';
import { nextMessageId, normalizeMessageId } from '../src/protocol/message-id';

describe('payload rules', () => {
  it('normalizes line endings and accepts only printable ASCII plus LF', () => {
    expect(prepareText('one\r\ntwo\rthree').display).toBe('one\ntwo\nthree');
    expect(prepareText('line 1\nline 2').bytes).toHaveLength(13);
    expect(() => prepareText('é')).toThrow(/ASCII/);
    expect(() => prepareText('漢')).toThrow(/ASCII/);
    expect(prepareText('a'.repeat(93)).bytes).toHaveLength(93);
    expect(() => prepareText('a'.repeat(94))).toThrow(/93/);
  });
  it('canonicalizes only HTTP and HTTPS URLs', () => {
    expect(prepareUrl('sonic.link/demo').display).toBe('https://sonic.link/demo');
    expect(prepareUrl('http://example.com').display).toBe('http://example.com/');
    expect(() => prepareUrl('javascript:alert(1)')).toThrow(/HTTP and HTTPS/);
    expect(() => prepareUrl('https://user:pass@example.com')).toThrow(/cannot be sent/);
  });
  it('keeps binary tokens and frame estimates exact', () => {
    expect([...parseToken('00 ff a4').bytes]).toEqual([0, 255, 164]);
    expect(parseToken('').bytes).toHaveLength(0);
    expect(frameCount(0)).toBe(1);
    expect(() => parseToken('1')).toThrow(/byte pairs/);
    expect(frameCount(31)).toBe(1); expect(frameCount(32)).toBe(2); expect(frameCount(93)).toBe(3);
    expect(estimatedSeconds(3)).toBeCloseTo(4.0);
  });
});

describe('message id lifecycle', () => {
  it('normalizes zero and wraps at 16 bits', () => {
    expect(normalizeMessageId(0)).toBe(1);
    expect(nextMessageId(65535)).toBe(1);
  });
});
