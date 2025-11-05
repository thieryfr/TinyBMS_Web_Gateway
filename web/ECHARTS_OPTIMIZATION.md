# 📦 Optimisation ECharts - Plan de Réduction

## 🎯 Objectif

Réduire la taille de `echarts.simple.min.js` de **482 KB à ~150-180 KB** (réduction de **60-70%**) sans perte de fonctionnalité.

---

## 📊 Analyse

### Composants ECharts actuellement utilisés

#### Types de séries (5)
- ✅ **LineChart** - Historique, sparklines (batterie, temps réel)
- ✅ **BarChart** - Distribution UART, débit CAN, messages MQTT, cellules
- ✅ **GaugeChart** - Jauge SOC/SOH de batterie
- ✅ **FunnelChart** - Flux MQTT
- ✅ **HeatmapChart** - Heatmap identifiants CAN

#### Composants fonctionnels (11)
- ✅ **TooltipComponent** - Infobulles interactives
- ✅ **GridComponent** - Positionnement des graphiques
- ✅ **LegendComponent** - Légendes
- ✅ **TitleComponent** - Messages d'état vide
- ✅ **DataZoomComponent** - Zoom/navigation historique
  - DataZoomInsideComponent (molette)
  - DataZoomSliderComponent (slider)
- ✅ **VisualMapComponent** - Carte de couleurs heatmap
- ✅ **MarkPointComponent** - Marqueurs de points
- ✅ **MarkLineComponent** - Lignes de référence
- ✅ **GraphicComponent** - Textes personnalisés

#### Renderers (2)
- ✅ **CanvasRenderer** - Par défaut (performances)
- ✅ **SVGRenderer** - Jauge uniquement

### Composants INUTILISÉS dans echarts.simple.min.js
❌ PieChart, ScatterChart, RadarChart, TreeChart, MapChart, GraphChart, etc.

---

## 🚀 Instructions de Build

### Étape 1 : Installer les dépendances

```bash
cd web/
npm install
```

Cela va installer :
- `echarts@5.5.1` - Bibliothèque ECharts complète (pour le build)
- `rollup@4.21.0` - Bundler
- `@rollup/plugin-node-resolve` - Résolution des modules
- `@rollup/plugin-terser` - Minification

### Étape 2 : Générer la version optimisée

```bash
npm run build:echarts
```

Cela va créer : `assets/js/echarts.custom.min.js` (~150-180 KB)

### Étape 3 : Remplacer dans index.html

#### Avant (index.html:17)
```html
<script src="../assets/js/echarts.simple.min.js" type="module"></script>
```

#### Après
```html
<script src="../assets/js/echarts.custom.min.js"></script>
```

**Note :** Retirer `type="module"` car la version bundlée est au format IIFE.

### Étape 4 : Tester

1. Ouvrir le dashboard dans le navigateur
2. Vérifier que tous les graphiques s'affichent :
   - ✅ Jauge SOC/SOH (batterie)
   - ✅ Sparkline tension/courant
   - ✅ Graphique cellules
   - ✅ Distribution UART
   - ✅ Heatmap CAN
   - ✅ Débit CAN
   - ✅ Historique avec zoom
   - ✅ Flux MQTT (funnel + bar)

---

## 🔍 Vérification de la taille

```bash
# Avant
ls -lh assets/js/echarts.simple.min.js
# -rw-r--r-- 1 user user 482K

# Après
ls -lh assets/js/echarts.custom.min.js
# -rw-r--r-- 1 user user ~150-180K  (réduction de 60-70%)
```

---

## 📝 Fichiers créés

1. **`build-echarts-custom.js`**
   - Script source avec imports modulaires
   - Liste explicite des composants utilisés

2. **`rollup.config.js`**
   - Configuration Rollup
   - Minification avec terser
   - Output format IIFE

3. **`package.json`** (mis à jour)
   - Ajout des devDependencies
   - Script `build:echarts`

---

## 🔧 Maintenance

### Ajouter un nouveau type de graphique

1. Ouvrir `build-echarts-custom.js`
2. Ajouter l'import correspondant :
   ```js
   import { PieChart } from 'echarts/charts';
   ```
3. L'ajouter dans `echarts.use([...])` :
   ```js
   echarts.use([
     // ...autres composants
     PieChart,
   ]);
   ```
4. Rebuild :
   ```bash
   npm run build:echarts
   ```

### Réduire encore plus la taille

Si certains composants ne sont finalement pas utilisés, les retirer de `build-echarts-custom.js` :

Par exemple, si vous n'utilisez jamais les marqueurs :
```js
// Commenter ou supprimer ces lignes
// import { MarkPointComponent } from 'echarts/components';
// import { MarkLineComponent } from 'echarts/components';
```

---

## ✅ Gains attendus

| Métrique | Avant | Après | Gain |
|----------|-------|-------|------|
| **Taille fichier** | 482 KB | ~150-180 KB | **60-70%** |
| **Temps de chargement** | ~1.2s (3G) | ~0.4s (3G) | **66%** |
| **Parse JS** | ~50ms | ~15-20ms | **60%** |
| **Fonctionnalités** | ✅ Toutes | ✅ Toutes | **100%** |

---

## 🎯 Alternative : CDN avec paramètres

Si vous ne voulez pas gérer le build localement, vous pouvez utiliser le CDN ECharts avec imports à la demande :

```html
<!-- Dans base.js, remplacer l'import global -->
<script type="module">
import * as echarts from 'https://cdn.jsdelivr.net/npm/echarts@5.5.1/dist/echarts.esm.min.js';
// Mais cela ne réduit PAS la taille du bundle
</script>
```

⚠️ **Pas recommandé** : Le fichier ESM complet fait ~900KB (pire que echarts.simple.min.js)

---

## 📚 Références

- [ECharts Custom Build Guide](https://echarts.apache.org/handbook/en/basics/import/)
- [Rollup Documentation](https://rollupjs.org/)
- [Terser Plugin Options](https://github.com/rollup/plugins/tree/master/packages/terser)

---

## 🐛 Dépannage

### Erreur : "echarts is not defined"

**Solution :** Vérifier que vous avez retiré `type="module"` du script tag dans index.html.

### Graphique ne s'affiche pas

**Solution :** Vérifier la console navigateur. Si erreur du type "registerChart is not a function", le composant manque dans `build-echarts-custom.js`.

### Build échoue

**Solution :**
```bash
rm -rf node_modules package-lock.json
npm install
npm run build:echarts
```

---

**Créé le :** 2025-01-05
**Auteur :** Optimisation TinyBMS Web Gateway
**Version :** 1.0
