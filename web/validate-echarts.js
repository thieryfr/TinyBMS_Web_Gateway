#!/usr/bin/env node
/**
 * Script de validation pour vérifier que tous les composants ECharts
 * nécessaires sont présents dans la version custom
 *
 * Usage :
 *   node validate-echarts.js
 */

import echarts from './build-echarts-custom.js';

const requiredCharts = [
  'line',
  'bar',
  'gauge',
  'funnel',
  'heatmap'
];

const requiredComponents = [
  'tooltip',
  'grid',
  'legend',
  'title',
  'dataZoom',
  'visualMap',
  'markPoint',
  'markLine',
  'graphic'
];

console.log('🔍 Validation de la configuration ECharts custom...\n');

let hasErrors = false;

// Créer un graphique de test pour chaque type
const testContainer = { clientWidth: 400, clientHeight: 300 };

console.log('📊 Test des types de séries :');
requiredCharts.forEach(chartType => {
  try {
    const chart = echarts.init(testContainer);
    chart.setOption({
      series: [{ type: chartType }]
    });
    console.log(`  ✅ ${chartType.padEnd(15)} - OK`);
    chart.dispose();
  } catch (error) {
    console.log(`  ❌ ${chartType.padEnd(15)} - MANQUANT`);
    console.log(`     Erreur: ${error.message}`);
    hasErrors = true;
  }
});

console.log('\n🔧 Test des composants :');
requiredComponents.forEach(component => {
  try {
    const chart = echarts.init(testContainer);
    const option = {};
    option[component] = {};
    chart.setOption(option);
    console.log(`  ✅ ${component.padEnd(15)} - OK`);
    chart.dispose();
  } catch (error) {
    console.log(`  ❌ ${component.padEnd(15)} - MANQUANT`);
    console.log(`     Erreur: ${error.message}`);
    hasErrors = true;
  }
});

console.log('\n🎨 Test des renderers :');
['canvas', 'svg'].forEach(renderer => {
  try {
    const chart = echarts.init(testContainer, null, { renderer });
    console.log(`  ✅ ${renderer.padEnd(15)} - OK`);
    chart.dispose();
  } catch (error) {
    console.log(`  ❌ ${renderer.padEnd(15)} - MANQUANT`);
    console.log(`     Erreur: ${error.message}`);
    hasErrors = true;
  }
});

console.log('\n' + '='.repeat(50));

if (hasErrors) {
  console.log('❌ ÉCHEC : Certains composants manquent');
  console.log('Vérifiez build-echarts-custom.js');
  process.exit(1);
} else {
  console.log('✅ SUCCÈS : Tous les composants sont présents');
  console.log('Vous pouvez builder la version custom !');
  console.log('\nCommande : npm run build:echarts');
  process.exit(0);
}
