# PLAN D'IMPLÉMENTATION DES CORRECTIONS
## TinyBMS-GW - Audit Front-end/Back-end

**Date:** 2025-01-10
**Branche de développement:** `claude/audit-frontend-backend-alignment-011CUyvodue6fWWHzASiixag`
**Référence:** Voir `RAPPORT_AUDIT_FRONTEND_BACKEND.md` pour les détails complets

---

## 📋 TÂCHES À IMPLÉMENTER

### TÂCHE 1: Corriger les endpoints API inexistants 🔴
**Priorité:** CRITIQUE
**Durée estimée:** 30 minutes
**Pull Request:** À créer - `fix/frontend-api-endpoints-alignment`

**Description:**
Le front-end appelle des endpoints `/api/tinybms/*` et `/api/monitoring/*` qui n'existent pas dans le back-end. Cela cause des erreurs 404 et empêche certaines fonctionnalités de fonctionner.

**Fichiers à modifier:**
- `web/src/components/tiny/tinybms-config.js` (2 corrections)
- `web/src/js/mqtt-config.js` (1 correction)
- `web/src/js/codeMetricsDashboard.js` (4 corrections)

**Corrections détaillées:**

#### 1.1 - tinybms-config.js ligne 903
```diff
- const response = await fetch('/api/tinybms/firmware/update', {
+ const response = await fetch('/api/ota', {
```

#### 1.2 - tinybms-config.js ligne 959
```diff
- const response = await fetch('/api/tinybms/restart', {
-     method: 'POST'
- });
+ const response = await fetch('/api/system/restart', {
+     method: 'POST',
+     headers: { 'Content-Type': 'application/json' },
+     body: JSON.stringify({ target: 'bms' })
+ });
```

#### 1.3 - mqtt-config.js ligne 5
```diff
- const SYSTEM_RUNTIME_ENDPOINTS = ['/api/monitoring/runtime', '/api/metrics/runtime'];
+ const SYSTEM_RUNTIME_ENDPOINTS = ['/api/metrics/runtime'];
```

#### 1.4 - codeMetricsDashboard.js lignes 6-9
```diff
  const API_ENDPOINTS = {
-     runtime: ['/api/monitoring/runtime', '/api/metrics/runtime'],
-     eventBus: ['/api/monitoring/event-bus', '/api/event-bus/metrics'],
-     tasks: ['/api/monitoring/tasks', '/api/system/tasks'],
-     modules: ['/api/monitoring/modules', '/api/system/modules'],
+     runtime: ['/api/metrics/runtime'],
+     eventBus: ['/api/event-bus/metrics'],
+     tasks: ['/api/system/tasks'],
+     modules: ['/api/system/modules'],
  };
```

**Tests de validation:**
- [ ] Vérifier qu'aucune erreur 404 n'apparaît dans la console navigateur
- [ ] Tester la mise à jour firmware TinyBMS via l'interface web
- [ ] Tester le redémarrage TinyBMS via l'interface web
- [ ] Vérifier que les métriques système s'affichent correctement

**Commande pour créer le PR:**
```bash
# Voir les changements
git diff web/src/components/tiny/tinybms-config.js web/src/js/mqtt-config.js web/src/js/codeMetricsDashboard.js

# Commiter les changements
git add web/src/components/tiny/tinybms-config.js web/src/js/mqtt-config.js web/src/js/codeMetricsDashboard.js
git commit -m "fix(frontend): align API endpoints with backend implementation

- Change /api/tinybms/firmware/update to /api/ota
- Change /api/tinybms/restart to /api/system/restart with target payload
- Remove non-existent /api/monitoring/* endpoints
- Keep only valid /api/metrics/* and /api/system/* endpoints

Fixes frontend 404 errors for firmware update, BMS restart, and metrics display.

Related to frontend-backend alignment audit."

# Pusher vers la branche
git push -u origin claude/audit-frontend-backend-alignment-011CUyvodue6fWWHzASiixag
```

**Lien PR à générer:** `https://github.com/thieryfr/TinyBMS-GW/pull/XXXX`

---

### TÂCHE 2: Utiliser les valeurs dynamiques du BMS 🟠
**Priorité:** HAUTE
**Durée estimée:** 45 minutes
**Pull Request:** À créer - `fix/frontend-use-bms-dynamic-values`

**Description:**
Le front-end utilise des constantes hardcodées (tensions, courants) au lieu d'utiliser les valeurs réelles configurées dans le TinyBMS. Cela cause un affichage incorrect des limites dans les graphiques.

**Fichiers à modifier:**
- `web/src/js/charts/batteryCharts.js` (4 corrections)

**Corrections détaillées:**

#### 2.1 - Corriger les noms de champs (lignes 650-651)
```diff
- const peak_discharge_a = registers.peak_discharge_current_a || DEFAULT_PEAK_DISCHARGE_A;
- const charge_overcurrent_a = registers.charge_overcurrent_a || DEFAULT_CHARGE_OVERCURRENT_A;
+ const peak_discharge_a = registers.peak_discharge_current_limit_a || DEFAULT_PEAK_DISCHARGE_A;
+ const charge_overcurrent_a = registers.charge_overcurrent_limit_a || DEFAULT_CHARGE_OVERCURRENT_A;
```

#### 2.2 - Utiliser les seuils de tension du BMS dans le constructeur (ligne 370-371)
```diff
  yAxis: {
      type: 'value',
-     min: UNDER_VOLTAGE_CUTOFF * 0.9,
-     max: OVER_VOLTAGE_CUTOFF * 1.1,
+     min: function (value) {
+         return value.min * 0.9;
+     },
+     max: function (value) {
+         return value.max * 1.1;
+     },
  },
```

#### 2.3 - Passer les registres à updateCellChart (ligne 711)
```diff
- this.updateCellChart(voltagesMv);
+ this.updateCellChart(voltagesMv, registers);
```

#### 2.4 - Utiliser les valeurs dynamiques dans updateCellChart (ligne 829)
```diff
- updateCellChart(voltagesMv) {
+ updateCellChart(voltagesMv, registers = {}) {
      if (!this.cellChart) {
          return;
      }

+     // Use dynamic cutoff values from BMS registers
+     const underVoltageCutoff = registers.undervoltage_cutoff_mv || DEFAULT_UNDERVOLTAGE_MV;
+     const overVoltageCutoff = registers.overvoltage_cutoff_mv || DEFAULT_OVERVOLTAGE_MV;

      // ... existing code ...

      markLine: {
          data: [
              {
                  name: 'Under-voltage',
-                 yAxis: UNDER_VOLTAGE_CUTOFF,
+                 yAxis: underVoltageCutoff,
                  label: {
-                     formatter: 'Under-voltage: {c} mV',
+                     formatter: `Under-voltage: ${underVoltageCutoff} mV`,
                  }
              },
              {
                  name: 'Over-voltage',
-                 yAxis: OVER_VOLTAGE_CUTOFF,
+                 yAxis: overVoltageCutoff,
                  label: {
-                     formatter: 'Over-voltage: {c} mV',
+                     formatter: `Over-voltage: ${overVoltageCutoff} mV`,
                  }
              }
          ]
      }
```

**Tests de validation:**
- [ ] Vérifier que les limites de tension correspondent aux valeurs configurées dans le TinyBMS
- [ ] Vérifier que les limites de courant correspondent aux valeurs du BMS
- [ ] Tester avec différentes configurations de BMS
- [ ] Vérifier que les graphiques s'adaptent automatiquement aux nouvelles limites

**Commande pour créer le PR:**
```bash
# Voir les changements
git diff web/src/js/charts/batteryCharts.js

# Commiter les changements
git add web/src/js/charts/batteryCharts.js
git commit -m "fix(frontend): use dynamic BMS values instead of hardcoded constants

- Fix field names: peak_discharge_current_limit_a, charge_overcurrent_limit_a
- Use BMS undervoltage_cutoff_mv and overvoltage_cutoff_mv in cell chart
- Pass registers to updateCellChart for dynamic markLine values
- Update yAxis limits to use dynamic values

Charts now display actual BMS configuration instead of hardcoded defaults.

Related to frontend-backend alignment audit."

# Pusher vers la branche
git push
```

**Lien PR à générer:** `https://github.com/thieryfr/TinyBMS-GW/pull/XXXX`

---

### TÂCHE 3: Implémenter le système de tooltips CAN 🟡
**Priorité:** MOYENNE
**Durée estimée:** 1 heure
**Pull Request:** À créer - `feat/frontend-can-tooltips`

**Description:**
Le HTML contient des attributs `data-tooltip` avec des IDs CAN (ex: "0x356") mais aucun JavaScript n'exploite ces attributs pour afficher des informations. Implémenter un système de tooltips pour améliorer l'UX.

**Nouveaux fichiers à créer:**
- `web/src/js/utils/canTooltips.js`

**Fichiers à modifier:**
- `web/src/js/dashboard.js` (ajouter l'import et l'initialisation)

**Implémentation:**

#### 3.1 - Créer canTooltips.js
```javascript
/**
 * CAN Protocol Tooltip System
 * Displays Victron CAN PGN descriptions on hover
 */

const CAN_DESCRIPTIONS = {
    '0x307': {
        name: 'Inverter Identifier',
        desc: 'Handshake avec Victron GX',
        interval: '1s',
        pgn: 'N/A'
    },
    '0x351': {
        name: 'Charge Parameters',
        desc: 'CVL, CCL, DCL (limites charge/décharge)',
        interval: '1s',
        pgn: 'N/A'
    },
    '0x355': {
        name: 'SOC & SOH',
        desc: 'État de charge et santé de la batterie',
        interval: '1s',
        pgn: 'N/A'
    },
    '0x356': {
        name: 'Voltage / Current / Temperature',
        desc: 'Tension, courant, température du pack',
        interval: '1s',
        pgn: 'N/A'
    },
    '0x35A': {
        name: 'Alarms & Warnings',
        desc: 'Alarmes et avertissements BMS',
        interval: '1s',
        pgn: '59904'
    },
    '0x35E': {
        name: 'Manufacturer Name',
        desc: 'Nom du fabricant (TinyBMS)',
        interval: 'Init',
        pgn: 'N/A'
    },
    '0x35F': {
        name: 'Battery Model & Capacity',
        desc: 'Modèle batterie, version firmware, capacité',
        interval: 'Init',
        pgn: 'N/A'
    },
    '0x372': {
        name: 'Module Status Counts',
        desc: 'Nombre de modules OK/Blocking/Offline',
        interval: '1s',
        pgn: 'N/A'
    },
    '0x373': {
        name: 'Cell Voltage & Temperature Extremes',
        desc: 'Min/Max tension et température des cellules',
        interval: '1s',
        pgn: 'N/A'
    },
    '0x378': {
        name: 'Cumulative Energy',
        desc: 'Énergie cumulée IN/OUT',
        interval: '1s',
        pgn: 'N/A'
    },
    '0x379': {
        name: 'Installed Capacity',
        desc: 'Capacité installée de la batterie',
        interval: 'Init',
        pgn: 'N/A'
    },
};

/**
 * Initialize CAN tooltips for all elements with data-tooltip attribute
 */
export function initCanTooltips() {
    const elements = document.querySelectorAll('[data-tooltip]');

    elements.forEach(element => {
        const canId = element.getAttribute('data-tooltip');
        const info = CAN_DESCRIPTIONS[canId];

        if (info) {
            // Set native HTML title attribute for simple tooltip
            const tooltipText = `${info.name} (${canId})\n${info.desc}\nInterval: ${info.interval}`;
            element.setAttribute('title', tooltipText);

            // Add visual indicator
            element.style.cursor = 'help';
            element.classList.add('has-can-tooltip');

            // Add a small icon indicator (optional)
            if (!element.querySelector('.can-tooltip-icon')) {
                const icon = document.createElement('i');
                icon.className = 'ti ti-info-circle can-tooltip-icon ms-1 text-muted';
                icon.style.fontSize = '0.875rem';
                icon.setAttribute('title', tooltipText);
                element.appendChild(icon);
            }
        }
    });

    console.log(`[CAN Tooltips] Initialized ${elements.length} tooltips`);
}

/**
 * Get CAN description by ID
 * @param {string} canId - CAN ID (e.g., "0x356")
 * @returns {Object|null} CAN description object or null
 */
export function getCanDescription(canId) {
    return CAN_DESCRIPTIONS[canId] || null;
}
```

#### 3.2 - Modifier dashboard.js
```diff
+ import { initCanTooltips } from './utils/canTooltips.js';

  // Dans la fonction d'initialisation (après le chargement du DOM)
+ // Initialize CAN protocol tooltips
+ initCanTooltips();
```

**Tests de validation:**
- [ ] Vérifier que les tooltips s'affichent au survol
- [ ] Vérifier que les descriptions sont correctes
- [ ] Tester sur différents navigateurs (Chrome, Firefox, Safari)
- [ ] Tester l'affichage sur mobile (tactile)

**Commande pour créer le PR:**
```bash
# Créer le nouveau fichier
# Modifier dashboard.js

# Voir les changements
git diff web/src/js/dashboard.js
git status

# Commiter les changements
git add web/src/js/utils/canTooltips.js web/src/js/dashboard.js
git commit -m "feat(frontend): implement CAN protocol tooltips

- Add canTooltips.js utility with Victron CAN descriptions
- Initialize tooltips on page load for all data-tooltip elements
- Display PGN name, description, and transmission interval on hover
- Add visual indicator (info icon) for elements with CAN tooltips

Improves user experience by explaining CAN protocol fields.

Related to frontend-backend alignment audit."

# Pusher vers la branche
git push
```

**Lien PR à générer:** `https://github.com/thieryfr/TinyBMS-GW/pull/XXXX`

---

## 📊 RÉCAPITULATIF

| Tâche | Priorité | Durée | Fichiers modifiés | Status |
|-------|----------|-------|-------------------|--------|
| T1: Endpoints API | 🔴 CRITIQUE | 30 min | 3 fichiers | ⏳ À faire |
| T2: Constantes dynamiques | 🟠 HAUTE | 45 min | 1 fichier | ⏳ À faire |
| T3: Tooltips CAN | 🟡 MOYENNE | 1h | 2 fichiers | ⏳ À faire |

**Durée totale estimée:** 2h15

---

## 🚀 WORKFLOW DE TRAVAIL

### 1. Créer la branche locale (si pas déjà fait)
```bash
git checkout -b claude/audit-frontend-backend-alignment-011CUyvodue6fWWHzASiixag
```

### 2. Implémenter les corrections
Pour chaque tâche:
1. Ouvrir les fichiers concernés
2. Appliquer les corrections détaillées ci-dessus
3. Tester localement
4. Commiter avec un message descriptif
5. Pusher vers la branche

### 3. Créer les Pull Requests
Une fois toutes les corrections commitées et pushées:

```bash
# Vérifier que tout est bien committé
git status

# Vérifier le log des commits
git log --oneline -5

# Pusher la branche vers GitHub
git push -u origin claude/audit-frontend-backend-alignment-011CUyvodue6fWWHzASiixag
```

Ensuite, créer manuellement les PRs sur GitHub avec:
- **Titre:** Résumé de la correction (ex: "fix(frontend): align API endpoints with backend")
- **Description:** Copier la section "Description" de chaque tâche
- **Labels:** `bug` pour T1 et T2, `enhancement` pour T3
- **Reviewers:** Assigner les reviewers appropriés
- **Linked issues:** Référencer l'audit dans la description

### 4. Tests de validation
Après chaque PR mergé, effectuer les tests de validation listés dans chaque tâche.

---

## 📝 NOTES IMPORTANTES

1. **Ne pas mélanger les corrections:** Chaque tâche doit être dans un commit séparé pour faciliter la revue
2. **Tester en local d'abord:** S'assurer que le serveur ESP32 et l'interface web fonctionnent correctement
3. **Vérifier la console navigateur:** Aucune erreur 404 ou autre erreur JS ne doit apparaître
4. **Documenter les changements:** Ajouter des commentaires si nécessaire pour expliquer pourquoi une constante a été changée

---

## 🔗 LIENS UTILES

- **Rapport d'audit complet:** `RAPPORT_AUDIT_FRONTEND_BACKEND.md`
- **Repository GitHub:** https://github.com/thieryfr/TinyBMS-GW
- **Documentation CAN Victron:** `docs/TinyBMS_CAN_BMS_mapping.json`
- **Protocole UART TinyBMS:** `main/uart_bms/uart_bms_protocol.h`
- **API Backend:** `main/web_server/web_server.c`

---

**Document créé par:** Claude (Anthropic Sonnet 4.5)
**Date:** 2025-01-10
**Branche:** `claude/audit-frontend-backend-alignment-011CUyvodue6fWWHzASiixag`
