export interface CoreModule {
  HEAPU8: Uint8Array;
  _malloc(size: number): number;
  _free(pointer: number): void;
  _sl_validate_payload(type: number, bytes: number, length: number): number;
  _sl_fragment(type: number, messageId: number, bytes: number, length: number, outFrames: number): number;
  _sl_reassembly_reset(state: number): void;
  _sl_reassembly_create(): number;
  _sl_reassembly_free(state: number): void;
  _sl_reassembly_accept(state: number, frame: number, nowMs: number, out: number): number;
  _sl_reassembly_poll(state: number, nowMs: number, out: number): number;
}

export async function createCore(baseUrl = new URL(import.meta.env.BASE_URL, window.location.href).href): Promise<CoreModule> {
  const wasmModuleUrl = new URL('wasm/sonic-core.js', baseUrl).href;
  const module = await import(/* @vite-ignore */ wasmModuleUrl);
  return (module.default as (options: unknown) => Promise<CoreModule>)({ locateFile: (file: string) => new URL(file, new URL('wasm/', baseUrl)).href });
}

const typeId: Record<string, number> = { TEXT: 1, URL: 2, TOKEN: 3, DEVICE_INFO: 4 };
export function validatePayload(core: CoreModule, kind: string, bytes: Uint8Array) {
  const ptr = core._malloc(Math.max(1, bytes.length));
  core.HEAPU8.set(bytes, ptr);
  const result = core._sl_validate_payload(typeId[kind] ?? 0, ptr, bytes.length);
  core._free(ptr);
  return result;
}

export function fragmentPayload(core: CoreModule, kind: string, id: number, bytes: Uint8Array): Uint8Array[] {
  const input = core._malloc(Math.max(1, bytes.length)), output = core._malloc(3 * 40);
  core.HEAPU8.set(bytes, input);
  const count = core._sl_fragment(typeId[kind] ?? 0, id, input, bytes.length, output);
  core._free(input);
  if (count < 1 || count > 3) { core._free(output); throw new Error('Payload is invalid for Sonic Link framing.'); }
  const result: Uint8Array[] = [];
  for (let i = 0; i < count; i++) result.push(core.HEAPU8.slice(output + i * 40, output + (i + 1) * 40));
  core._free(output);
  return result;
}
