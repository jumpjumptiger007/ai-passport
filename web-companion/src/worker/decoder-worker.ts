import { decodeFrames } from './decoder-runtime';

type Request = { type: 'START'; sampleRate: number } | { type: 'PCM'; samples: Int16Array } | { type: 'STOP' };
const send = (event: Record<string, unknown>, transfer?: Transferable[]) => self.postMessage(event, transfer ?? []);
self.onmessage = async (event: MessageEvent<Request>) => {
  try { await decodeFrames(event.data, send); }
  catch (error) { send({ type: 'ERROR', state: 'wasm-error', reason: error instanceof Error ? error.message : 'Decoder failed.' }); }
};
