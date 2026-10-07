import './styles.css';
import { AudioSession } from './audio/audio-session';
import { OneShotDebugCapture } from './audio/debug-capture';
import { downloadWav } from './audio/wav';
import { TxPlayer } from './audio/tx-player';
import { frameCount, estimatedSeconds, hex, parseToken, prepareText, prepareUrl } from './protocol/payload';
import { nextMessageId } from './protocol/message-id';
import { fragmentPayload, createCore } from './protocol/wasm-core';
import { initialState, reduce, reusablePreparedSend, type WebState } from './state/web-state';

const app = document.querySelector<HTMLDivElement>('#app')!;
let state: WebState = initialState();
let core: Awaited<ReturnType<typeof createCore>> | undefined;
let version = '0.1.0';
let buildId = 'unbuilt';
let rx = new AudioSession();
let tx = new TxPlayer();
const capture = new OneShotDebugCapture();
let latestAudio: { sampleRate: number; tracks: MediaStreamTrack[] } | undefined;

const esc = (s: string) => s.replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]!);
const mutate = (action: Parameters<typeof reduce>[1], draw = true) => { state = reduce(state, action); if (draw) render(); };
const prepared = () => state.kind === 'TEXT' ? prepareText(state.input) : state.kind === 'URL' ? prepareUrl(state.input) : parseToken(state.input);

function signalBars(level: number | undefined) {
  const factors = [0.24, 0.43, 0.68, 1, 0.76, 0.49];
  const heights = factors.map(factor => level === undefined ? 6 : Math.max(5, Math.round(5 + Math.sqrt(level) * 76 * factor)));
  return `<div class="signal ${level === undefined ? 'inactive' : ''}" role="img" aria-label="${level === undefined ? 'Microphone level not measured yet' : 'Live microphone level'}">${heights.map(height => `<i style="height:${height}px"></i>`).join('')}</div>`;
}

function render() {
  const payload = (() => { try { return prepared(); } catch { return undefined; } })();
  const details = payload ? `<div class="meta"><span>${payload.bytes.length} / 93 bytes</span><span>${frameCount(payload.bytes.length)} frame${frameCount(payload.bytes.length) === 1 ? '' : 's'} · ~${estimatedSeconds(frameCount(payload.bytes.length)).toFixed(1)} s</span></div>` : '';
  const nav = `<header class="head"><button class="brand" data-nav="home">SONIC LINK</button><span class="head-version">${esc(version)}</span></header>`;
  let body = '';
  if (state.screen === 'home') body = `<section class="hero"><h1>Move small data through sound.</h1><p>No pairing. No account. Encoding and decoding stay on this device.</p></section><section class="content"><button class="action active" data-nav="send"><strong>Send to Passport <span>→</span></strong><p>Text, short URL or binary token.</p></button><button class="action" data-nav="receive"><strong>Receive from Passport <span>→</span></strong><p>Listen for a nearby transmission.</p></button><button class="action" data-nav="diagnostics"><strong>Diagnostics <span>→</span></strong><p>Audio state, debug tools and last transfer.</p></button></section>`;
  if (state.screen === 'send') body = `<div class="eyebrow">SONIC LINK · SEND</div><section class="hero compact"><h1>Send to Passport</h1><p>Keep the phone near the Passport while sound is playing.</p></section><section class="content"><div class="tabs">${(['TEXT','URL','TOKEN'] as const).map(k => `<button class="tab ${state.kind === k ? 'on' : ''}" data-kind="${k}">${k === 'TEXT' ? 'Text' : k === 'URL' ? 'URL' : 'Token'}</button>`).join('')}</div><div class="field"><label for="payload">${state.kind === 'TEXT' ? 'Message' : state.kind === 'URL' ? 'URL' : 'Token · hexadecimal'}</label>${state.kind === 'URL' ? `<input id="payload" value="${esc(state.input)}" autocomplete="url" />` : `<textarea id="payload" class="${state.kind === 'TOKEN' ? 'hex' : ''}">${esc(state.input)}</textarea>`}</div>${details}<div id="validation" class="notice err" hidden></div>${state.kind === 'URL' && payload ? `<div class="card"><small>Will send</small><strong>${esc(payload.display)}</strong></div>` : ''}<button class="btn" data-send>Send with sound</button>${state.kind === 'TOKEN' ? '<button class="btn secondary spacer" data-generate>Generate test token</button>' : ''}<div class="card tip"><small>Tip</small><p>Use medium-high media volume for best results. Transmission completion does not confirm reception.</p></div></section>`;
  if (state.screen === 'sending') body = `<div class="eyebrow">SONIC LINK · TX</div><section class="hero compact"><h1>Playing sound</h1><p>Keep the phone near the Passport until transmission finishes.</p></section><section class="content center"><div class="big">${state.txFrame} / ${state.txFrames}</div><div class="bar"><b style="width:${state.txFrames ? Math.round(state.txFrame / state.txFrames * 100) : 0}%"></b></div><div class="meta"><span>Frame ${state.txFrame} of ${state.txFrames}</span><span>Playback progress</span></div><button class="btn secondary" data-cancel>Cancel</button></section>`;
  if (state.screen === 'sent') body = `<div class="eyebrow">SONIC LINK · TX</div><section class="hero compact"><span class="status ok">Transmit complete</span><h1 class="spaced">Sent</h1><p>Sound transmission completed.</p></section><section class="content"><div class="card"><small>Note</small><strong>Reception is not acknowledged.</strong><p>The Passport may not have received the sound. You can resend this same message.</p></div><button class="btn" data-send-again>Send again</button><button class="btn secondary spacer" data-edit>Change message</button></section>`;
  if (state.screen === 'interrupted') body = `<div class="eyebrow">SONIC LINK · TX</div><section class="hero compact"><span class="status warn">Interrupted</span><h1 class="spaced">Not completed</h1><p>${esc(state.lastError)}</p></section><section class="content"><button class="btn" data-send-again>Try again</button><button class="btn secondary spacer" data-nav="send">Back to message</button></section>`;
  if (state.screen === 'receive') body = `${receiveBody()}`;
  if (state.screen === 'result') body = resultBody();
  if (state.screen === 'diagnostics') body = diagnosticsBody();
  app.innerHTML = `<main class="phone"><div class="app">${nav}<div id="screen">${body}</div><footer>Static PWA · Local processing${state.screen === 'receive' && state.rx === 'listening' ? ' · Mic active' : ''}</footer></div></main>`;
  bind();
}

function receiveBody() {
  const statusCopy: Record<string, [string, string]> = {
    idle: ['Receive from Passport', 'The browser needs microphone access to decode a nearby Sonic Link message.'],
    requesting: ['Requesting microphone', 'Allow microphone access to begin local decoding.'],
    listening: ['Listening', 'Waiting for sound from a nearby Passport.'],
    assembling: ['Receiving', 'A Sonic Link message has started. Keep the Passport nearby.'],
    incomplete: ['Message incomplete', state.lastError || 'The message could not be completed. Keep listening or try again.'],
    paused: ['Listening paused', 'The page was hidden. The microphone has been stopped; resume when ready.'],
    'permission-denied': ['Microphone unavailable', 'Microphone permission was denied. Change browser site settings, then try again.'],
    'no-microphone': ['No microphone found', 'Connect or enable a microphone and try again.'],
    'worklet-error': ['Audio capture unavailable', state.lastError || 'This browser could not start AudioWorklet capture.'],
    'wasm-error': ['Decoder unavailable', state.lastError || 'The local decoder could not be initialized.'],
    unsupported: ['Unsupported browser', 'Use a recent mobile Safari or Chrome browser with AudioWorklet support.'],
  };
  const [title, message] = statusCopy[state.rx];
  return `<div class="eyebrow">SONIC LINK · RECEIVE</div><section class="hero compact"><h1>${esc(title)}</h1><p>${esc(message)}</p></section><section class="content">${state.rx === 'idle' || state.rx === 'paused' ? `<div class="card"><small>Privacy</small><strong>Audio stays on this device.</strong><p>Receive audio is decoded locally and discarded unless one-shot debug capture is enabled.</p></div><button class="btn" data-listen>${state.rx === 'paused' ? 'Resume listening' : 'Start listening'}</button>` : ''}${state.rx === 'requesting' ? '<div class="card">Waiting for microphone permission…</div>' : ''}${state.rx === 'listening' || state.rx === 'assembling' ? `<div class="center">${signalBars(state.rxLevel)}<p class="listen-label">${state.rx === 'assembling' ? `${state.rxReceived} / ${state.rxTotal} frames` : 'Listening'}</p><div class="muted">AudioContext ${latestAudio?.sampleRate ?? '…'} Hz · local decode</div></div><button class="btn secondary" data-stop>Stop listening</button>` : ''}${state.rx === 'incomplete' ? '<button class="btn" data-continue>Keep listening</button><button class="btn secondary spacer" data-stop>Stop listening</button>' : ''}${['permission-denied','no-microphone','worklet-error','wasm-error','unsupported'].includes(state.rx) ? '<button class="btn" data-listen>Try again</button>' : ''}<div class="notice">${esc(state.lastError)}</div></section>`;
}

function resultBody() {
  const result = state.result!; const bytes = result.bytes;
  if (result.kind === 'DEVICE_INFO') {
    const b = bytes;
    return `<div class="eyebrow">SONIC LINK · RECEIVE</div><section class="hero compact"><span class="status ok">Device info received</span><h1 class="spaced">AI Passport</h1><p>Validated device information.</p></section><section class="content"><div class="card diag"><div><span>Model</span><span>${b[1]}</span></div><div><span>Firmware</span><span>${b[2]}.${b[3]}.${b[4]}</span></div><div><span>ggwave</span><span>${b[5]}.${b[6]}.${b[7]}</span></div><div><span>Battery</span><span>${b[8]}%</span></div><div><span>Profile</span><span>${b[9]}</span></div><div><span>Fingerprint</span><span>${hex(b.slice(12, 16)).replaceAll(' ', '')}</span></div></div><button class="btn secondary" data-listen-again>Listen again</button></section>`;
  }
  const text = result.kind === 'TOKEN' ? '' : new TextDecoder().decode(bytes);
  const safeUrl = result.kind === 'URL' ? text : '';
  const action = result.kind === 'TOKEN' ? `<div class="card"><small>Hexadecimal · ${bytes.length} bytes</small><strong class="hex">${esc(hex(bytes))}</strong></div><button class="btn" data-copy-token>Copy Hex</button>` : `<div class="card"><small>${esc(result.kind)}</small><strong>${esc(text)}</strong></div>${result.kind === 'URL' ? '<button class="btn" data-open-url>Open URL</button><button class="btn secondary spacer" data-copy>Copy</button>' : '<button class="btn" data-copy>Copy</button>'}`;
  return `<div class="eyebrow">SONIC LINK · RECEIVE</div><section class="hero compact"><span class="status ok">${esc(result.kind)} received</span><h1 class="spaced">Received</h1><p>Complete message passed framing, CRC and type validation.</p></section><section class="content">${action}<button class="btn secondary spacer" data-listen-again>Listen again</button><div class="notice" id="copy-notice" role="status"></div></section>`;
}

function diagnosticsBody() {
  return `<div class="eyebrow">SONIC LINK · DIAGNOSTICS</div><section class="hero compact"><h1>Diagnostics</h1><p>Browser and local audio information for this session.</p></section><section class="content"><div class="card diag"><div><span>Web Companion</span><span>${esc(version)}</span></div><div><span>Build ID</span><span>${esc(buildId)}</span></div><div><span>Protocol</span><span>v1 · 40-byte frame</span></div><div><span>Max payload</span><span>93 UTF-8 bytes</span></div><div><span>Modem profile</span><span>AUDIBLE_FASTEST candidate</span></div><div><span>Requested mic processing</span><span>Echo cancel / AGC / noise suppression off</span></div><div><span>AudioContext rate</span><span>${latestAudio?.sampleRate ? `${latestAudio.sampleRate} Hz` : 'Not started'}</span></div><div><span>Observed mic rate</span><span>${latestAudio?.tracks?.[0]?.getSettings().sampleRate ?? 'Not started'}</span></div><div><span>WASM</span><span>${core ? 'Loaded' : 'Loads on demand'}</span></div><div><span>Browser</span><span>${esc(navigator.userAgent.slice(0, 68))}</span></div></div><div class="card"><small>One-shot debug capture</small><p>Off by default. Captures only the next receive, in memory, up to 30 seconds. No upload or automatic save.</p><button class="btn secondary" data-debug>${state.debugArmed ? 'Cancel debug capture' : 'Capture next receive once'}</button><div class="meta"><span>${capture.sampleCount ? `${(capture.sampleCount / (latestAudio?.sampleRate || 1)).toFixed(1)} s captured` : 'No debug audio stored'}</span></div>${capture.sampleCount ? '<button class="btn" data-download>Download WAV</button><button class="btn secondary spacer" data-clear>Clear capture</button>' : ''}</div><div class="card"><small>Release pairing</small><p>Firmware release identity pairing and deployment are handled in the next release phase.</p></div></section>`;
}

function bind() {
  app.querySelectorAll<HTMLElement>('[data-nav]').forEach(el => el.onclick = () => mutate({ type: 'NAVIGATE', screen: el.dataset.nav as WebState['screen'] }));
  app.querySelectorAll<HTMLButtonElement>('[data-kind]').forEach(el => el.onclick = () => mutate({ type: 'EDIT', kind: el.dataset.kind as WebState['kind'], input: el.dataset.kind === 'TEXT' ? '' : '' }));
  const input = app.querySelector<HTMLInputElement | HTMLTextAreaElement>('#payload');
  input?.addEventListener('input', () => {
    state = reduce(state, { type: 'EDIT', input: input.value });
    const meta = app.querySelector('.meta');
    try { const p = prepared(); const count = frameCount(p.bytes.length); if (meta) meta.innerHTML = `<span>${p.bytes.length} / 93 bytes</span><span>${count} frame${count === 1 ? '' : 's'} · ~${estimatedSeconds(count).toFixed(1)} s</span>`; }
    catch { if (meta) meta.textContent = 'Payload must be valid and no more than 93 bytes.'; }
  });
  app.querySelector<HTMLButtonElement>('[data-send]')?.addEventListener('click', () => { void send(); });
  app.querySelector<HTMLButtonElement>('[data-send-again]')?.addEventListener('click', () => { void send(); });
  app.querySelector<HTMLButtonElement>('[data-edit]')?.addEventListener('click', () => mutate({ type: 'NEW_MESSAGE' }));
  app.querySelector<HTMLButtonElement>('[data-generate]')?.addEventListener('click', () => {
    const bytes = crypto.getRandomValues(new Uint8Array(16)); mutate({ type: 'EDIT', kind: 'TOKEN', input: hex(bytes) });
  });
  app.querySelector<HTMLButtonElement>('[data-cancel]')?.addEventListener('click', () => { void tx.cancel(); mutate({ type: 'TX_INTERRUPTED', reason: 'Transmission cancelled before playback completed.' }); });
  app.querySelector<HTMLButtonElement>('[data-listen]')?.addEventListener('click', () => { void startReceive(); });
  app.querySelector<HTMLButtonElement>('[data-stop]')?.addEventListener('click', () => { void stopReceive(); });
  app.querySelector<HTMLButtonElement>('[data-continue]')?.addEventListener('click', () => mutate({ type: 'RX_STATE', state: 'listening', reason: '' }));
  app.querySelector<HTMLButtonElement>('[data-listen-again]')?.addEventListener('click', () => { void startReceive(); });
  app.querySelector<HTMLButtonElement>('[data-debug]')?.addEventListener('click', () => {
    if (state.debugArmed) capture.clear();
    mutate({ type: 'DEBUG_ARM', value: !state.debugArmed });
  });
  app.querySelector<HTMLButtonElement>('[data-download]')?.addEventListener('click', () => downloadWav(capture.toWav()));
  app.querySelector<HTMLButtonElement>('[data-clear]')?.addEventListener('click', () => { capture.clear(); render(); });
  app.querySelector<HTMLButtonElement>('[data-copy],[data-copy-token]')?.addEventListener('click', async () => {
    const r = state.result!; const value = r.kind === 'TOKEN' ? hex(r.bytes) : new TextDecoder().decode(r.bytes);
    try { await navigator.clipboard.writeText(value); app.querySelector('#copy-notice')!.textContent = 'Copied to clipboard.'; }
    catch { app.querySelector('#copy-notice')!.textContent = 'Clipboard access unavailable. Select and copy the value.'; }
  });
  app.querySelector<HTMLButtonElement>('[data-open-url]')?.addEventListener('click', () => {
    const url = new URL(new TextDecoder().decode(state.result!.bytes));
    if (url.protocol === 'http:' || url.protocol === 'https:') window.open(url.href, '_blank', 'noopener,noreferrer');
  });
}

async function send() {
  const validation = app.querySelector<HTMLElement>('#validation');
  let p;
  try { p = prepared(); }
  catch (error) { if (validation) { validation.hidden = false; validation.textContent = error instanceof Error ? error.message : 'Invalid payload.'; } return; }
  try {
    core ??= await createCore();
    let preparedSend = reusablePreparedSend(state, p.kind, state.input);
    if (!preparedSend) {
      const messageId = nextMessageId(state.messageId);
      const frames = fragmentPayload(core, p.kind, messageId, p.bytes);
      state = reduce(state, { type: 'TX_PREPARED', kind: p.kind, input: state.input, messageId, frames });
      preparedSend = reusablePreparedSend(state, p.kind, state.input);
    }
    if (!preparedSend) throw new Error('Could not prepare the logical message for transmission.');
    mutate({ type: 'TX_START', frames: preparedSend.frames.length });
    await tx.send(preparedSend.frames, progress => {
      if (progress.complete) mutate({ type: 'TX_COMPLETE' }); else mutate({ type: 'TX_FRAME', frame: progress.frame });
    }, new URL(import.meta.env.BASE_URL, window.location.href).href);
  } catch (error) {
    mutate({ type: 'TX_INTERRUPTED', reason: error instanceof Error ? error.message : 'Transmission interrupted.' });
    app.insertAdjacentHTML('beforeend', `<div class="toast" role="status">${esc(state.lastError)}</div>`);
  }
}

async function startReceive() {
  mutate({ type: 'RX_STATE', state: 'requesting' });
  try {
    const audio = await rx.startReceive({
      baseUrl: new URL(import.meta.env.BASE_URL, window.location.href).href,
      debugCapture: state.debugArmed,
      onLevel: level => {
        state = reduce(state, { type: 'RX_LEVEL', level });
        const liveSignal = app.querySelector('.signal');
        if (liveSignal) liveSignal.outerHTML = signalBars(state.rxLevel);
      },
      onPcm: (pcm: Float32Array) => {
        if (!capture.isArmed) return;
        const rate = latestAudio?.sampleRate ?? 48000; const samples = new Int16Array(pcm.length);
        for (let i = 0; i < pcm.length; i++) samples[i] = Math.max(-1, Math.min(1, pcm[i])) * (pcm[i] < 0 ? 32768 : 32767);
        capture.push(samples); if (!capture.isArmed) capture.finish();
      },
      onEvent: event => {
        if (event.type === 'READY') mutate({ type: 'RX_STATE', state: 'listening' });
        if (event.type === 'PROGRESS') mutate({ type: 'RX_PROGRESS', received: event.received, total: event.total });
        if (event.type === 'INCOMPLETE') mutate({ type: 'RX_STATE', state: 'incomplete', reason: event.reason });
        if (event.type === 'EXPIRED') { capture.finish(); mutate({ type: 'RX_STATE', state: 'incomplete', reason: event.reason }); }
        if (event.type === 'RESULT') {
          capture.finish(); void rx.stop();
          const kind = ['?', 'TEXT', 'URL', 'TOKEN', 'DEVICE_INFO'][event.payloadType] ?? 'UNKNOWN';
          mutate({ type: 'RX_RESULT', kind, bytes: new Uint8Array(event.bytes), messageId: event.messageId });
        }
        if (event.type === 'ERROR') { void rx.stop(); capture.finish(); mutate({ type: 'RX_LEVEL', level: undefined }, false); mutate({ type: 'RX_STATE', state: event.state ?? 'incomplete', reason: event.reason }); }
      },
    });
    latestAudio = audio;
    if (state.debugArmed) { capture.arm(audio.sampleRate); mutate({ type: 'DEBUG_ARM', value: false }); }
  } catch (error) {
    await rx.stop();
    const message = error instanceof Error ? error.message : 'Microphone initialization failed.';
    const name = error instanceof DOMException ? error.name : '';
    const stateName = /denied|permission/i.test(message) || name === 'NotAllowedError' ? 'permission-denied' : /device|microphone/i.test(message) || name === 'NotFoundError' ? 'no-microphone' : /support|unsupported/i.test(message) ? 'unsupported' : /wasm|decoder/i.test(message) ? 'wasm-error' : 'worklet-error';
    if (state.debugArmed) { capture.clear(); mutate({ type: 'DEBUG_ARM', value: false }, false); }
    mutate({ type: 'RX_STATE', state: stateName, reason: message });
  }
}

async function stopReceive() { await rx.stop(); capture.finish(); mutate({ type: 'RX_LEVEL', level: undefined }, false); mutate({ type: 'RX_STATE', state: 'idle' }); }

document.addEventListener('visibilitychange', () => {
  if (document.visibilityState !== 'hidden') return;
  if (state.rx === 'listening' || state.rx === 'assembling' || state.rx === 'incomplete') { void rx.suspend(); capture.finish(); mutate({ type: 'RX_LEVEL', level: undefined }, false); mutate({ type: 'RX_STATE', state: 'paused', reason: 'Microphone stopped while the page is hidden.' }); }
  if (state.tx === 'playing') { void tx.cancel(); mutate({ type: 'TX_INTERRUPTED', reason: 'Transmission interrupted because the page was hidden.' }); }
});

async function boot() {
  try { const response = await fetch('./version.json'); if (response.ok) { const identity = await response.json(); version = identity.version; buildId = identity.buildId; } } catch { /* version is build-time metadata */ }
  if ('serviceWorker' in navigator && import.meta.env.PROD) void navigator.serviceWorker.register('./sw.js');
  render();
}
void boot();
