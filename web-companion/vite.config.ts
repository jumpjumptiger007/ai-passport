import { defineConfig } from 'vite';

export default defineConfig({
  base: './',
  build: {
    target: 'es2022',
    assetsInlineLimit: 0,
    sourcemap: false,
    emptyOutDir: true,
  },
  worker: { format: 'es' },
  server: { host: '127.0.0.1' },
});
