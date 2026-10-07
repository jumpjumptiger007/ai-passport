import { describe, expect, it } from 'vitest';
import { nextMessageId } from '../src/protocol/message-id';
import { initialState, reduce, reusablePreparedSend } from '../src/state/web-state';

describe('test_web_state', () => {
  it('seeds a nonzero randomized sequence and sends deterministic wrapped IDs', () => {
    expect(initialState(0).messageId).toBe(1);
    expect(initialState(0xffff).messageId).toBe(0xffff);
    expect(nextMessageId(initialState(0xffff).messageId)).toBe(1);
  });

  it('models accepted and denied microphone permission, missing devices and unsupported APIs', () => {
    expect(reduce(initialState(9), { type: 'RX_STATE', state: 'requesting' }).rx).toBe('requesting');
    expect(reduce(initialState(9), { type: 'RX_STATE', state: 'listening' }).rx).toBe('listening');
    for (const state of ['permission-denied', 'no-microphone', 'unsupported'] as const) {
      expect(reduce(initialState(9), { type: 'RX_STATE', state }).rx).toBe(state);
    }
  });

  it('surfaces AudioWorklet and WASM initialization failures as recoverable states', () => {
    expect(reduce(initialState(1), { type: 'RX_STATE', state: 'worklet-error', reason: 'AudioWorklet failed' }).lastError).toBe('AudioWorklet failed');
    expect(reduce(initialState(1), { type: 'RX_STATE', state: 'wasm-error', reason: 'WASM failed' }).rx).toBe('wasm-error');
  });

  it('tracks only measured microphone level and clamps the value', () => {
    expect(reduce(initialState(1), { type: 'RX_LEVEL', level: undefined }).rxLevel).toBeUndefined();
    expect(reduce(initialState(1), { type: 'RX_LEVEL', level: 2 }).rxLevel).toBe(1);
    expect(reduce(initialState(1), { type: 'RX_LEVEL', level: -1 }).rxLevel).toBe(0);
  });

  it('retains real assembly progress through incomplete and Listen Again', () => {
    let state = reduce(initialState(1), { type: 'RX_PROGRESS', received: 1, total: 3 });
    expect([state.rx, state.rxReceived, state.rxTotal]).toEqual(['assembling', 1, 3]);
    state = reduce(state, { type: 'RX_STATE', state: 'incomplete', reason: 'timeout' });
    expect([state.rxReceived, state.rxTotal]).toEqual([1, 3]);
    state = reduce(state, { type: 'RX_STATE', state: 'listening' });
    expect(state.rx).toBe('listening');
  });

  it('pauses and resumes hidden RX without discarding the receive flow', () => {
    let state = reduce(initialState(1), { type: 'RX_STATE', state: 'listening' });
    state = reduce(state, { type: 'RX_STATE', state: 'paused', reason: 'hidden' });
    expect(state.rx).toBe('paused');
    state = reduce(state, { type: 'RX_STATE', state: 'listening' });
    expect(state.rx).toBe('listening');
  });

  it('keeps prepared ID and exact frames for Send Again but allocates a new ID after edits', () => {
    const firstFrames = [Uint8Array.from([1, 2, 3])];
    let state = initialState(0xfffe);
    state = reduce(state, { type: 'TX_PREPARED', kind: 'TEXT', input: state.input, messageId: 0xffff, frames: firstFrames });
    const first = reusablePreparedSend(state)!;
    expect(first.messageId).toBe(0xffff);
    expect(reusablePreparedSend(state)?.frames).toBe(firstFrames);
    state = reduce(state, { type: 'TX_INTERRUPTED', reason: 'cancelled' });
    expect(reusablePreparedSend(state)?.messageId).toBe(0xffff);
    expect(reusablePreparedSend(state)?.frames).toBe(firstFrames);
    state = reduce(state, { type: 'EDIT', input: 'A different logical message' });
    expect(reusablePreparedSend(state)).toBeUndefined();
    state = reduce(state, { type: 'TX_PREPARED', kind: 'TEXT', input: state.input, messageId: 1, frames: [Uint8Array.from([4])] });
    expect(reusablePreparedSend(state)?.messageId).toBe(1);
    state = reduce(state, { type: 'EDIT', kind: 'URL', input: 'https://example.com/' });
    expect(reusablePreparedSend(state)).toBeUndefined();
    state = reduce(state, { type: 'TX_PREPARED', kind: 'URL', input: state.input, messageId: 2, frames: [Uint8Array.from([5])] });
    expect(reusablePreparedSend(state)?.messageId).toBe(2);
    state = reduce(state, { type: 'NEW_MESSAGE' });
    expect(reusablePreparedSend(state)).toBeUndefined();
  });

  it('marks cancellation or hidden TX as Interrupted, never Complete', () => {
    for (const reason of ['cancelled', 'hidden']) {
      let state = reduce(initialState(2), { type: 'TX_START', frames: 3 });
      state = reduce(state, { type: 'TX_INTERRUPTED', reason });
      expect(state.tx).toBe('interrupted');
      expect(state.screen).toBe('interrupted');
    }
  });

  it('requires playback completion for Done and supports deterministic result and Diagnostics navigation', () => {
    let state = reduce(initialState(5), { type: 'TX_START', frames: 2 });
    state = reduce(state, { type: 'TX_FRAME', frame: 1 });
    expect(state.tx).toBe('playing');
    state = reduce(state, { type: 'TX_COMPLETE' });
    expect([state.tx, state.screen]).toEqual(['complete', 'sent']);
    state = reduce(state, { type: 'RX_RESULT', kind: 'TEXT', bytes: new TextEncoder().encode('ok'), messageId: 8 });
    expect([state.screen, state.result?.messageId]).toEqual(['result', 8]);
    state = reduce(state, { type: 'NAVIGATE', screen: 'diagnostics' });
    expect(state.screen).toBe('diagnostics');
  });
});
