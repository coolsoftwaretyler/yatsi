import { defineConfig } from 'vite';
import { resolve } from 'path';
import fs from 'fs';
import { viteStaticCopy } from 'vite-plugin-static-copy';

const wasmExists = fs.existsSync('src/engine/wasm/yatsi.wasm');

export default defineConfig({
  base: './',
  build: {
    target: 'esnext',
    rollupOptions: {
      input: {
        index: resolve(__dirname, 'index.html'),
        custom: resolve(__dirname, 'custom.html'),
      },
    },
  },
  worker: {
    format: 'es',
  },
  plugins: [
    ...(wasmExists
      ? [
          viteStaticCopy({
            targets: [
              {
                src: 'src/engine/wasm/yatsi.wasm',
                dest: 'assets',
              },
            ],
          }),
        ]
      : []),
  ],
  server: {
    headers: {
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp',
    },
    proxy: {
      '/api/test262-zip': {
        target: 'https://codeload.github.com',
        changeOrigin: true,
        rewrite: () => '/nicolo-ribaudo/test262/zip/refs/heads/main',
      },
    },
  },
  optimizeDeps: {
    exclude: ['src/engine/wasm/yatsi.js'],
  },
});
