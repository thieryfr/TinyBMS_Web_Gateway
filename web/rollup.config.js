/**
 * Configuration Rollup pour créer une version minimale d'ECharts
 *
 * Installation des dépendances :
 *   npm install --save-dev echarts rollup @rollup/plugin-node-resolve @rollup/plugin-terser
 *
 * Build :
 *   npx rollup -c rollup.config.js
 */

import { nodeResolve } from '@rollup/plugin-node-resolve';
import terser from '@rollup/plugin-terser';

export default {
  input: 'build-echarts-custom.js',
  output: {
    file: 'assets/js/echarts.custom.min.js',
    format: 'iife',
    name: 'echarts',
    sourcemap: false,
    compact: true
  },
  plugins: [
    nodeResolve(),
    terser({
      compress: {
        drop_console: false, // Garder console pour debugging si besoin
        passes: 3,
        pure_funcs: ['console.debug']
      },
      mangle: {
        reserved: ['echarts']
      },
      format: {
        comments: false
      }
    })
  ]
};
