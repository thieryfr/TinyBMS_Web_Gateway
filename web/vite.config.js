// vite.config.js
import { defineConfig } from 'vite';

export default defineConfig({
  build: {
    outDir: 'dist',
    minify: 'esbuild',
    assetsDir: 'assets',
    rollupOptions: {
      input: 'index.html'
    }
  }
});
