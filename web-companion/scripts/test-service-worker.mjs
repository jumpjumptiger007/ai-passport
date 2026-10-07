import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';
import { cacheNameFor } from './postbuild.mjs';

const dist = new URL('../dist/', import.meta.url);
const [serviceWorker, identity] = await Promise.all([
  readFile(new URL('sw.js', dist), 'utf8'),
  readFile(new URL('version.json', dist), 'utf8').then(JSON.parse),
]);
const current = cacheNameFor(identity.packageVersion, identity.buildId);
const old = cacheNameFor(identity.packageVersion, 'oldbuild000001');
assert.notEqual(old, current, 'two source builds have distinct cache identities');

let existing = [old, current, 'unrelated-cache'];
const removed = [];
const handlers = new Map();
const self = {
  addEventListener(type, listener) { handlers.set(type, listener); },
  clients: { claim: async () => {} },
  skipWaiting: async () => {},
};
const caches = {
  keys: async () => [...existing],
  delete: async name => { removed.push(name); existing = existing.filter(item => item !== name); return true; },
  open: async () => ({ addAll: async () => {}, put: async () => {} }),
  match: async () => undefined,
};
vm.runInNewContext(serviceWorker, { self, caches, URL, Promise });
let activation;
handlers.get('activate')({ waitUntil(promise) { activation = promise; } });
await activation;
assert.deepEqual(removed, [old], 'activation removes the older build cache only');
assert.ok(existing.includes(current), 'activation preserves the current build cache');
assert.ok(existing.includes('unrelated-cache'), 'activation preserves unrelated application caches');
console.log(`PWA service worker: ${old} → ${current}; stale-build cleanup PASS`);
