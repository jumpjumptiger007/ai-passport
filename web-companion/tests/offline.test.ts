import { describe, expect, it } from 'vitest';
import { buildIdForAssets, cacheNameFor, cacheableBuildFiles, isObsoleteCache } from '../scripts/postbuild.mjs';

describe('versioned offline asset list', () => {
  it('keeps worker, worklet, wasm and application assets while excluding debug audio', () => {
    const paths = cacheableBuildFiles(['index.html', 'assets/app.js', 'assets/decoder-worker.js', 'assets/capture-processor.js', 'wasm/ggwave.wasm', 'debug-capture.wav', 'assets/app.js.map']);
    expect(paths).toEqual(['index.html', 'assets/app.js', 'assets/decoder-worker.js', 'assets/capture-processor.js', 'wasm/ggwave.wasm']);
  });

  it('derives identity from sorted production content and deletes only older Sonic Link build caches', () => {
    const first = buildIdForAssets([{ path: 'app.js', content: 'one' }, { path: 'style.css', content: 'same' }]);
    const same = buildIdForAssets([{ path: 'style.css', content: 'same' }, { path: 'app.js', content: 'one' }]);
    const changed = buildIdForAssets([{ path: 'app.js', content: 'two' }, { path: 'style.css', content: 'same' }]);
    expect(first).toBe(same);
    expect(changed).not.toBe(first);
    const oldCache = cacheNameFor('0.1.0', first);
    const activeCache = cacheNameFor('0.1.0', changed);
    expect(oldCache).not.toBe(activeCache);
    expect(isObsoleteCache(oldCache, activeCache)).toBe(true);
    expect(isObsoleteCache(activeCache, activeCache)).toBe(false);
    expect(isObsoleteCache('other-app-cache', activeCache)).toBe(false);
  });
});
