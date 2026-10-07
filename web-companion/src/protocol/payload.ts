export type PayloadKind = 'TEXT' | 'URL' | 'TOKEN';
export const MAX_BYTES = 93;
const PASSPORT_TEXT = /^[\x20-\x7e\n]*$/u;

export interface PreparedPayload { kind: PayloadKind; bytes: Uint8Array; display: string; }

export function prepareText(value: string): PreparedPayload {
  const normalized = value.replace(/\r\n?/g, '\n');
  if (!PASSPORT_TEXT.test(normalized)) throw new Error('Use ASCII characters and line breaks supported by AI Passport display.');
  return bounded('TEXT', new TextEncoder().encode(normalized), normalized);
}

export function prepareUrl(value: string): PreparedPayload {
  const raw = value.trim();
  if (!raw) throw new Error('Enter a URL.');
  let parsed: URL;
  try { parsed = new URL(/^[a-z][a-z\d+.-]*:/i.test(raw) ? raw : `https://${raw}`); }
  catch { throw new Error('Enter a valid HTTP or HTTPS URL.'); }
  if (parsed.protocol !== 'http:' && parsed.protocol !== 'https:') throw new Error('Only HTTP and HTTPS URLs can be sent.');
  if (!parsed.hostname || parsed.username || parsed.password) throw new Error('This URL cannot be sent.');
  return bounded('URL', new TextEncoder().encode(parsed.href), parsed.href);
}

export function parseToken(value: string): PreparedPayload {
  const compact = value.replace(/\s+/g, '');
  if (compact.length % 2 || !/^[\da-f]*$/i.test(compact)) throw new Error('Enter complete hexadecimal byte pairs.');
  const bytes = Uint8Array.from(compact.match(/../g) ?? [], pair => Number.parseInt(pair, 16));
  return bounded('TOKEN', bytes, hex(bytes), 0);
}

function bounded(kind: PayloadKind, bytes: Uint8Array, display: string, minBytes = 1): PreparedPayload {
  if (bytes.length < minBytes || bytes.length > MAX_BYTES) throw new Error(`Payload must be ${minBytes === 0 ? '0' : '1'}–${MAX_BYTES} bytes.`);
  return { kind, bytes, display };
}

export function hex(bytes: Uint8Array): string { return [...bytes].map(b => b.toString(16).padStart(2, '0').toUpperCase()).join(' '); }
export function frameCount(byteLength: number): number { return Math.max(1, Math.ceil(byteLength / 31)); }
export function estimatedSeconds(count: number): number { return count * 1.2 + Math.max(0, count - 1) * 0.2; }
