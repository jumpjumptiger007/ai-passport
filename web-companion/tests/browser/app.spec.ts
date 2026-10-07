import { expect, test } from '@playwright/test';

test('home, navigation, validation, and local test token', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByRole('heading', { name: 'Move small data through sound.' })).toBeVisible();
  await page.getByRole('button', { name: /Send to Passport/ }).click();
  await expect(page.getByRole('heading', { name: 'Send to Passport' })).toBeVisible();
  await page.getByRole('button', { name: 'Token' }).click();
  await page.getByRole('button', { name: 'Generate test token' }).click();
  await expect(page.locator('#payload')).toHaveValue(/[A-F0-9]{2}/);
  await expect(page.getByText(/16 \/ 93 bytes/)).toBeVisible();
});

test('Diagnostics shows the package and content-derived build identity', async ({ page }) => {
  await page.goto('/');
  await page.getByRole('button', { name: /Diagnostics/ }).click();
  await expect(page.locator('.head-version')).toHaveText(/0\.1\.0\+[a-f0-9]{12}/);
  await expect(page.locator('.diag div').filter({ hasText: 'Build ID' })).toContainText(/Build ID/);
});

test('microphone permission denial is recoverable', async ({ page, context }) => {
  await page.addInitScript(() => {
    Object.defineProperty(navigator, 'mediaDevices', { configurable: true, value: { getUserMedia: async () => { throw new DOMException('Permission denied', 'NotAllowedError'); } } });
  });
  await page.goto('/');
  await page.getByRole('button', { name: /Receive from Passport/ }).click();
  await page.getByRole('button', { name: 'Start listening' }).click();
  await expect(page.getByRole('heading', { name: 'Microphone unavailable' })).toBeVisible();
  await expect(page.getByRole('button', { name: 'Try again' })).toBeVisible();
});

test('renders a deterministic validated receive result from the worker boundary', async ({ page, context }) => {
  await context.grantPermissions(['microphone']);
  await page.addInitScript(() => {
    class FixtureWorker {
      onmessage = (_event: any) => {};
      emit(data: any) { this.onmessage({ data }); }
      postMessage(message: { type: string }) {
        if (message.type === 'START') {
          setTimeout(() => this.emit({ type: 'READY' }), 0);
          setTimeout(() => this.emit({ type: 'RESULT', payloadType: 1, messageId: 7, bytes: new TextEncoder().encode('Fixture message') }), 50);
        }
      }
      terminate() {}
    }
    Object.defineProperty(window, 'Worker', { configurable: true, value: FixtureWorker });
  });
  await page.goto('/');
  await page.getByRole('button', { name: /Receive from Passport/ }).click();
  await page.getByRole('button', { name: 'Start listening' }).click();
  await expect(page.getByRole('heading', { name: 'Received' })).toBeVisible({ timeout: 10000 });
  await expect(page.getByText('Fixture message')).toBeVisible();
  await expect(page.getByRole('button', { name: 'Copy' })).toBeVisible();
});

test('production offline cache includes local application, worker, worklet, and WASM assets', async ({ page, context }) => {
  const externalRequests: string[] = [];
  page.on('request', request => { if (!request.url().startsWith('http://127.0.0.1:4173')) externalRequests.push(request.url()); });
  await page.goto('/');
  const cached = await page.evaluate(async () => {
    await navigator.serviceWorker.ready;
    const names = await caches.keys();
    const requests = await Promise.all(names.map(async name => (await caches.open(name)).keys()));
    const identity = await fetch('./version.json').then(response => response.json());
    return { names, identity, urls: requests.flat().map(request => new URL(request.url).pathname) };
  });
  const currentCache = `sonic-link-${cached.identity.version}`;
  expect(cached.identity.buildId).toMatch(/^[a-f0-9]{12}$/);
  expect(cached.identity.version).toBe(`0.1.0+${cached.identity.buildId}`);
  expect(cached.names).toContain(currentCache);
  expect(cached.urls.some(url => url.includes('decoder-worker'))).toBe(true);
  expect(cached.urls.some(url => url.includes('capture-processor'))).toBe(true);
  expect(cached.urls.some(url => url.endsWith('/wasm/sonic-core.wasm'))).toBe(true);
  expect(cached.urls.some(url => url.endsWith('/wasm/ggwave.wasm'))).toBe(true);
  expect(cached.urls.some(url => url.endsWith('.wav'))).toBe(false);
  expect(externalRequests).toEqual([]);
  await page.reload();
  await page.waitForFunction(() => navigator.serviceWorker.controller !== null);
  await context.setOffline(true);
  await page.reload();
  await expect(page.getByRole('heading', { name: 'Move small data through sound.' })).toBeVisible();
  await context.setOffline(false);
});

test('hidden page pauses receive and releases the microphone track', async ({ page, context }) => {
  await context.grantPermissions(['microphone']);
  await page.addInitScript(() => {
    (window as any).__trackStops = 0;
    const stop = MediaStreamTrack.prototype.stop;
    MediaStreamTrack.prototype.stop = function () { (window as any).__trackStops++; return stop.call(this); };
    class FixtureWorker {
      onmessage = (_event: any) => {};
      postMessage(message: { type: string }) { if (message.type === 'START') setTimeout(() => this.onmessage({ data: { type: 'READY' } }), 0); }
      terminate() {}
    }
    Object.defineProperty(window, 'Worker', { configurable: true, value: FixtureWorker });
  });
  await page.goto('/');
  await page.getByRole('button', { name: /Receive from Passport/ }).click();
  await page.getByRole('button', { name: 'Start listening' }).click();
  await expect(page.getByRole('heading', { name: 'Listening' })).toBeVisible({ timeout: 10000 });
  await page.evaluate(() => {
    Object.defineProperty(document, 'visibilityState', { configurable: true, value: 'hidden' });
    document.dispatchEvent(new Event('visibilitychange'));
  });
  await expect(page.getByRole('heading', { name: 'Listening paused' })).toBeVisible();
  await expect.poll(() => page.evaluate(() => (window as any).__trackStops)).toBeGreaterThan(0);
});

test('text input validation prevents unsupported Unicode from reaching transmit', async ({ page }) => {
  await page.goto('/');
  await page.getByRole('button', { name: /Send to Passport/ }).click();
  await page.locator('#payload').fill('漢字');
  await page.getByRole('button', { name: 'Send with sound' }).click();
  await expect(page.getByText(/Use ASCII characters/)).toBeVisible();
  await expect(page.getByRole('heading', { name: 'Playing sound' })).toHaveCount(0);
});
