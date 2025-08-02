
// vite.config.js
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      '/api': { target: 'http://127.0.0.1:3490', changeOrigin: true },
      // Proxy the contract in dev so frontend always reads backend's contract
      '^/routes\.json$': { target: 'http://127.0.0.1:3490', changeOrigin: true }
    }
  }
});