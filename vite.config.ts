import { defineConfig } from 'vite';
import fs from 'fs';
import { viteStaticCopy } from 'vite-plugin-static-copy';

const wasmExists = fs.existsSync('src/engine/wasm/yatsi.wasm');

export default defineConfig({
  base: './',
  build: {
    target: 'esnext',
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
  },
  optimizeDeps: {
    exclude: ['src/engine/wasm/yatsi.js'],
  },
});
