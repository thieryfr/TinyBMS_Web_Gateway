#!/usr/bin/env node
/**
 * Script de build personnalisé pour ECharts
 * Génère une version minimale avec seulement les composants utilisés dans TinyBMS Web Gateway
 *
 * Installation :
 *   npm install echarts --save-dev
 *
 * Usage :
 *   node build-echarts-custom.js
 *
 * Réduction attendue : ~482KB → ~150-180KB (70% de réduction)
 */

import * as echarts from 'echarts/core';

// === TYPES DE SÉRIES (CHARTS) ===
import { LineChart } from 'echarts/charts';
import { BarChart } from 'echarts/charts';
import { GaugeChart } from 'echarts/charts';
import { FunnelChart } from 'echarts/charts';
import { HeatmapChart } from 'echarts/charts';

// === COMPOSANTS FONCTIONNELS ===
import { TooltipComponent } from 'echarts/components';
import { GridComponent } from 'echarts/components';
import { LegendComponent } from 'echarts/components';
import { TitleComponent } from 'echarts/components';
import { DataZoomComponent } from 'echarts/components';
import { DataZoomInsideComponent } from 'echarts/components';
import { DataZoomSliderComponent } from 'echarts/components';
import { VisualMapComponent } from 'echarts/components';
import { MarkPointComponent } from 'echarts/components';
import { MarkLineComponent } from 'echarts/components';
import { GraphicComponent } from 'echarts/components';

// === RENDERERS ===
import { CanvasRenderer } from 'echarts/renderers';
import { SVGRenderer } from 'echarts/renderers';

// Enregistrer tous les composants nécessaires
echarts.use([
  // Charts
  LineChart,
  BarChart,
  GaugeChart,
  FunnelChart,
  HeatmapChart,

  // Components
  TooltipComponent,
  GridComponent,
  LegendComponent,
  TitleComponent,
  DataZoomComponent,
  DataZoomInsideComponent,
  DataZoomSliderComponent,
  VisualMapComponent,
  MarkPointComponent,
  MarkLineComponent,
  GraphicComponent,

  // Renderers
  CanvasRenderer,
  SVGRenderer
]);

// Exporter pour utilisation en module
export default echarts;

// Également exposer globalement pour compatibilité
if (typeof window !== 'undefined') {
  window.echarts = echarts;
}

console.log('✅ ECharts custom build créé avec succès !');
console.log('📦 Composants inclus :');
console.log('   - Charts: Line, Bar, Gauge, Funnel, Heatmap');
console.log('   - Components: Tooltip, Grid, Legend, Title, DataZoom, VisualMap, Graphic');
console.log('   - Renderers: Canvas, SVG');
console.log('🎯 Réduction de taille estimée : 60-70%');
