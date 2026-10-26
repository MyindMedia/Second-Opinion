import { defineConfig } from 'vite';

// Output is embedded in the plugin (BinaryData zip) and served by the WebBrowserComponent
// ResourceProvider, so: relative base, flat folder, stable names, no external URLs.
export default defineConfig({
  base: './',
  build: {
    outDir: 'dist',
    assetsDir: '',
    emptyOutDir: true,
    target: 'es2020',
    cssCodeSplit: false,
    sourcemap: false,
    assetsInlineLimit: 0,
    rollupOptions: {
      output: {
        entryFileNames: 'app.js',
        chunkFileNames: '[name].js',
        assetFileNames: '[name][extname]',
      },
    },
  },
  server: { port: 3000, strictPort: true },
  test: { environment: 'node' },
} as any);
