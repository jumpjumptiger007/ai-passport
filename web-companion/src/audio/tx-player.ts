export type TxProgress = { frame: number; total: number; complete: boolean };
type ModemFactory = (opts?: unknown) => Promise<any>;

export class TxPlayer {
  private context?: AudioContext;
  private modem: any;
  private instance = -1;
  private source?: AudioBufferSourceNode;
  private cancelled = false;

  async send(frames: Uint8Array[], onProgress: (progress: TxProgress) => void, baseUrl = new URL(import.meta.env.BASE_URL, window.location.href).href) {
    this.cancelled = false;
    try {
      this.context = new AudioContext();
      if (this.context.state === 'suspended') await this.context.resume();
      const wasmModuleUrl = new URL('wasm/ggwave.js', baseUrl).href;
      const module = await import(/* @vite-ignore */ wasmModuleUrl);
      this.modem = await (module.default as ModemFactory)({ locateFile: (p: string) => new URL(p, new URL('wasm/', baseUrl)).href });
      this.modem._sl_ggwave_quiet();
      this.instance = this.modem._sl_ggwave_create_tx(this.context.sampleRate);
      if (this.instance < 0) throw new Error('Could not initialize the pinned ggwave transmitter.');
      for (let i = 0; i < frames.length; i++) {
        if (this.cancelled || document.visibilityState === 'hidden') throw new Error('Transmission interrupted.');
        const frame = frames[i];
        const framePtr = this.modem._malloc(40); this.modem.HEAPU8.set(frame, framePtr);
        const samplePtrSlot = this.modem._malloc(4);
        const count = this.modem._sl_ggwave_encode(this.instance, framePtr, samplePtrSlot);
        const samplePtr = this.modem.HEAPU32[samplePtrSlot >>> 2];
        if (count <= 0 || !samplePtr) { this.modem._free(framePtr); this.modem._free(samplePtrSlot); throw new Error('Could not encode a 40-byte Sonic Link frame.'); }
        const samples = this.modem.HEAP16.slice(samplePtr >>> 1, (samplePtr >>> 1) + count);
        this.modem._sl_ggwave_free_samples(samplePtr); this.modem._free(framePtr); this.modem._free(samplePtrSlot);
        const buffer = this.context.createBuffer(1, samples.length, this.context.sampleRate);
        const channel = buffer.getChannelData(0);
        for (let j = 0; j < samples.length; j++) channel[j] = samples[j] / 32768;
        const source = this.context.createBufferSource(); this.source = source; source.buffer = buffer; source.connect(this.context.destination);
        const ended = new Promise<void>((resolve, reject) => {
          source.onended = () => this.cancelled ? reject(new Error('Transmission interrupted.')) : resolve();
        });
        source.start();
        await ended;
        onProgress({ frame: i + 1, total: frames.length, complete: false });
        if (i + 1 < frames.length) await new Promise(resolve => setTimeout(resolve, 200));
      }
      onProgress({ frame: frames.length, total: frames.length, complete: true });
    } catch (error) {
      await this.dispose();
      throw error;
    }
    await this.dispose();
  }

  async cancel() { this.cancelled = true; try { this.source?.stop(); } catch { /* already ended */ } await this.dispose(); }
  private async dispose() {
    if (this.instance >= 0) this.modem?._sl_ggwave_free(this.instance);
    this.instance = -1;
    if (this.context && this.context.state !== 'closed') await this.context.close();
    this.context = undefined; this.source = undefined;
  }
}
