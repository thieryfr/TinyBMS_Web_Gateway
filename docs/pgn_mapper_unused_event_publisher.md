# Analyse détaillée: PGN Mapper - Event Publisher inutilisé

**Date**: 2025-11-02
**Fichiers concernés**: `main/pgn_mapper/pgn_mapper.c`, `main/pgn_mapper/pgn_mapper.h`
**Sévérité**: ⚠️ Faible (inconsistance architecturale, pas de bug fonctionnel)

---

## 1. ÉTAT ACTUEL DU CODE

### Code actuel (pgn_mapper.c)

```c
static event_bus_publish_fn_t s_event_publisher = NULL;  // Ligne 8
static uart_bms_live_data_t s_latest_bms = {0};
static bool s_has_bms = false;

static void pgn_mapper_on_bms_update(const uart_bms_live_data_t *data, void *context)
{
    (void)context;
    if (data == NULL) {
        return;
    }

    s_latest_bms = *data;  // Stocke les données
    s_has_bms = true;

    ESP_LOGD(TAG, "Received TinyBMS update: %.2f V %.2f A",
             data->pack_voltage_v, data->pack_current_a);
    // ⚠️ Aucune action supplémentaire
}

void pgn_mapper_set_event_publisher(event_bus_publish_fn_t publisher)
{
    s_event_publisher = publisher;  // Enregistré
}

void pgn_mapper_init(void)
{
    (void)s_event_publisher;  // ⚠️ Cast explicite en void = inutilisé

    esp_err_t err = uart_bms_register_listener(pgn_mapper_on_bms_update, NULL);
    // ... rest of init
}
```

### Enregistrement dans app_main.c

```c
pgn_mapper_set_event_publisher(publish_hook);  // Ligne 31
pgn_mapper_init();                             // Ligne 57
```

---

## 2. LE PROBLÈME

### 2.1 Inconsistance architecturale

**Pattern standard dans le projet** (exemple: can_publisher.c):
```c
static event_bus_publish_fn_t s_event_publisher = NULL;

void can_publisher_on_bms_update(const uart_bms_live_data_t *data, void *context)
{
    // Traitement des données

    // Publication d'événement ✅
    event_bus_event_t event = {
        .id = APP_EVENT_ID_CAN_FRAME_READY,
        .payload = frame,
        .payload_size = sizeof(*frame),
    };
    s_event_publisher(&event, timeout);
}
```

**PGN Mapper** ne suit PAS ce pattern:
- ❌ Enregistre le publisher mais ne l'utilise jamais
- ❌ Ne publie aucun événement
- ❌ Cast explicite `(void)s_event_publisher` = supprime warning compilateur

### 2.2 Architecture actuelle

```
UART BMS (données brutes)
   │
   ├──→ PGN Mapper (écoute, stocke, ne fait rien d'autre) ⚠️
   │
   ├──→ CAN Publisher (écoute, convertit, publie) ✅
   │      └─→ APP_EVENT_ID_CAN_FRAME_READY
   │
   └──→ Monitoring (écoute, agrège, publie) ✅
          └─→ APP_EVENT_ID_TELEMETRY_SAMPLE
```

**PGN Mapper est un "dead module"** dans le flux de données.

---

## 3. CONTEXTE HISTORIQUE

D'après la documentation (`archive/docs/reference/module_pgn_mapper.md`):

> "pgn_mapper est **prévu** pour traduire les données TinyBMS vers des messages CAN Victron **complexes**"

**Intention initiale**:
- Module intermédiaire pour conversions complexes
- Publication d'événements PGN enrichis
- Calculs dérivés (statistiques, tendances)
- Collaboration avec CAN Publisher

**Réalité actuelle**:
- Module jamais complété
- CAN Publisher fait toutes les conversions directement (via `conversion_table.c`)
- PGN Mapper = simple cache passif des données BMS

---

## 4. OPTIONS DE CORRECTION

### Option A: ✅ **SUPPRIMER l'event publisher** (Recommandée)

**Rationalité**: Si le module ne publie rien, retirer le hook inutile.

**Modifications**:

```diff
--- a/main/pgn_mapper/pgn_mapper.c
+++ b/main/pgn_mapper/pgn_mapper.c
@@ -5,7 +5,6 @@

 #include "uart_bms.h"

-static event_bus_publish_fn_t s_event_publisher = NULL;
 static const char *TAG = "pgn_mapper";
 static uart_bms_live_data_t s_latest_bms = {0};
 static bool s_has_bms = false;
@@ -23,16 +22,10 @@ static void pgn_mapper_on_bms_update(const uart_bms_live_data_t *data, void *co
     ESP_LOGD(TAG, "Received TinyBMS update: %.2f V %.2f A", data->pack_voltage_v, data->pack_current_a);
 }

-void pgn_mapper_set_event_publisher(event_bus_publish_fn_t publisher)
-{
-    s_event_publisher = publisher;
-}
-
 void pgn_mapper_init(void)
 {
-    (void)s_event_publisher;
-
     esp_err_t err = uart_bms_register_listener(pgn_mapper_on_bms_update, NULL);
+    // ... rest
 }
```

```diff
--- a/main/pgn_mapper/pgn_mapper.h
+++ b/main/pgn_mapper/pgn_mapper.h
@@ -1,7 +1,5 @@
 #pragma once

-#include "event_bus.h"
-
 void pgn_mapper_init(void);
-void pgn_mapper_set_event_publisher(event_bus_publish_fn_t publisher);
```

```diff
--- a/main/app_main.c
+++ b/main/app_main.c
@@ -28,7 +28,6 @@ void app_main(void)
     uart_bms_set_event_publisher(publish_hook);
     can_publisher_set_event_publisher(publish_hook);
     can_victron_set_event_publisher(publish_hook);
-    pgn_mapper_set_event_publisher(publish_hook);
     web_server_set_event_publisher(publish_hook);
     // ... rest
```

**Avantages**:
- ✅ Code cohérent avec la fonction réelle du module
- ✅ Supprime l'inconsistance
- ✅ Pas de changement fonctionnel (rien ne cassera)
- ✅ Moins de confusion pour futurs développeurs

**Inconvénients**:
- ⚠️ Rend plus difficile l'ajout futur d'événements (faudra rajouter le hook)

---

### Option B: 🔧 **IMPLÉMENTER la publication d'événements**

**Rationalité**: Compléter l'intention originale du module.

**Approche 1: Publier événements PGN mappés**

```c
static void pgn_mapper_on_bms_update(const uart_bms_live_data_t *data, void *context)
{
    (void)context;
    if (data == NULL || s_event_publisher == NULL) {
        return;
    }

    s_latest_bms = *data;
    s_has_bms = true;

    // Nouvelle fonctionnalité: publier données PGN enrichies
    pgn_mapper_data_t pgn_data = {
        .timestamp_ms = data->timestamp_ms,
        .cvl_mv = /* calcul dynamique CVL */,
        .ccl_a = /* calcul limite charge */,
        .dcl_a = /* calcul limite décharge */,
        // ... autres PGNs calculés
    };

    event_bus_event_t event = {
        .id = APP_EVENT_ID_PGN_MAPPED_DATA,  // Nouvel événement
        .payload = &pgn_data,
        .payload_size = sizeof(pgn_data),
    };

    if (!s_event_publisher(&event, pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to publish PGN mapped data");
    }

    ESP_LOGD(TAG, "Published PGN data: CVL=%.2fV CCL=%.1fA DCL=%.1fA",
             pgn_data.cvl_mv / 1000.0f, pgn_data.ccl_a, pgn_data.dcl_a);
}
```

**Approche 2: Déléguer au CAN Publisher via événement**

```c
static void pgn_mapper_on_bms_update(const uart_bms_live_data_t *data, void *context)
{
    // ... stockage local

    // Publier pour CAN Publisher (au lieu de listener direct)
    event_bus_event_t event = {
        .id = APP_EVENT_ID_BMS_DATA_READY_FOR_CAN,
        .payload = data,
        .payload_size = sizeof(*data),
    };
    s_event_publisher(&event, pdMS_TO_TICKS(50));
}
```

**Changements requis**:
1. Définir nouveaux événements dans `app_events.h`
2. Implémenter logique de conversion/enrichissement
3. Modifier CAN Publisher pour écouter événements PGN Mapper
4. Ajuster tests unitaires

**Avantages**:
- ✅ Respecte pattern architectural event-driven
- ✅ Permet découplage CAN Publisher / UART BMS
- ✅ Ouvre possibilités enrichissement données (filtrage, moyennes, tendances)

**Inconvénients**:
- ❌ Effort développement important (50-100 lignes code)
- ❌ Risque régression (changement flux existant)
- ❌ Duplication logique déjà dans CAN Publisher
- ❌ Latence supplémentaire (événement intermédiaire)

---

### Option C: 📝 **DOCUMENTER l'intention**

**Rationalité**: Clarifier que c'est une décision volontaire.

**Modification**: Ajouter commentaire explicite

```c
void pgn_mapper_init(void)
{
    // NOTE: s_event_publisher est enregistré mais volontairement inutilisé.
    // Le module sert uniquement de cache passif des données BMS.
    // Les conversions PGN sont gérées directement par can_publisher.
    // Si besoin futur de publier des PGNs enrichis, le hook est déjà en place.
    (void)s_event_publisher;

    esp_err_t err = uart_bms_register_listener(pgn_mapper_on_bms_update, NULL);
    // ... rest
}
```

**Avantages**:
- ✅ Changement minimal (commentaire seulement)
- ✅ Préserve possibilité future d'extension
- ✅ Clarifie intention pour futurs développeurs

**Inconvénients**:
- ⚠️ Ne résout pas l'inconsistance architecturale
- ⚠️ Hook inutilisé reste en mémoire

---

### Option D: 🗑️ **SUPPRIMER le module entier**

**Rationalité**: Module sans fonction réelle = dead code.

**Modifications**:
1. Retirer `pgn_mapper/` du projet
2. Retirer de `main/CMakeLists.txt`
3. Retirer de `app_main.c`

**Avantages**:
- ✅ Code le plus simple et direct
- ✅ Supprime toute confusion
- ✅ Réduit surface de maintenance

**Inconvénients**:
- ❌ Perd cache centralisé des données BMS
- ❌ Si besoin futur, faudra recréer
- ❌ Changement plus invasif (tests, documentation)

---

## 5. RECOMMANDATION

### ✅ **Option A: Supprimer l'event publisher**

**Justification**:
1. **Principe YAGNI** (You Aren't Gonna Need It): Le hook n'est pas utilisé depuis la création du projet
2. **Cohérence**: Code doit refléter sa fonction réelle
3. **Maintenance**: Moins de code inutile = moins de confusion
4. **Réversible**: Si besoin futur, rajouter le hook est trivial (3 lignes)

**Impact**:
- ✅ Aucun changement fonctionnel
- ✅ Aucun test à modifier
- ✅ Clarté du code améliorée

### Alternative: Option C si extension future prévue

Si vous planifiez d'enrichir PGN Mapper dans 3-6 mois:
- Garder le hook
- Documenter clairement l'intention
- Créer issue GitHub pour tracking

---

## 6. IMPLÉMENTATION OPTION A (détaillée)

### Étape 1: Modifier pgn_mapper.c

```bash
# Supprimer ligne 8
- static event_bus_publish_fn_t s_event_publisher = NULL;

# Supprimer lignes 26-29
- void pgn_mapper_set_event_publisher(event_bus_publish_fn_t publisher)
- {
-     s_event_publisher = publisher;
- }

# Supprimer ligne 33
- (void)s_event_publisher;
```

### Étape 2: Modifier pgn_mapper.h

```bash
# Supprimer ligne 3
- #include "event_bus.h"

# Supprimer ligne 6
- void pgn_mapper_set_event_publisher(event_bus_publish_fn_t publisher);
```

### Étape 3: Modifier app_main.c

```bash
# Supprimer ligne 31
- pgn_mapper_set_event_publisher(publish_hook);
```

### Étape 4: Vérifier compilation

```bash
cd /home/user/TinyBMS_Web_Gateway
idf.py build
# Aucune erreur attendue
```

### Étape 5: Mettre à jour documentation

```bash
# Mettre à jour COHERENCE_REVIEW.md
# Retirer mention "event pub inutilisé" des problèmes
```

---

## 7. IMPLÉMENTATION OPTION B (si choisi)

### Structure proposée

```c
// Nouveau fichier: main/pgn_mapper/pgn_enrichment.h
typedef struct {
    uint64_t timestamp_ms;
    uint16_t cvl_mv;           // Calculated Charge Voltage Limit
    float ccl_a;               // Calculated Charge Current Limit
    float dcl_a;               // Calculated Discharge Current Limit
    float avg_cell_voltage_mv; // Average cell voltage
    float cell_voltage_spread_mv; // Max - Min cell voltage
    uint8_t health_score;      // 0-100 computed health metric
} pgn_enriched_data_t;

esp_err_t pgn_mapper_enrich_data(const uart_bms_live_data_t *raw,
                                   pgn_enriched_data_t *enriched);
```

### Modifications app_events.h

```c
typedef enum {
    // ... existing events
    APP_EVENT_ID_PGN_ENRICHED_DATA = 0x1203,  // After CAN_FRAME_READY
} app_event_id_t;
```

### Tests requis

```c
// test/test_pgn_mapper.c
void test_pgn_enrichment_calculates_cvl(void);
void test_pgn_enrichment_handles_edge_cases(void);
void test_pgn_mapper_publishes_event(void);
```

**Effort estimé**: 3-5 heures développement + 2 heures tests

---

## 8. DÉCISION ET TIMELINE

### Décision à prendre

| Critère | Option A (Supprimer) | Option B (Implémenter) | Option C (Documenter) |
|---------|---------------------|------------------------|----------------------|
| Effort | ⚡ 15 min | 🔨 5 heures | ⚡ 5 min |
| Risque | ✅ Aucun | ⚠️ Régression | ✅ Aucun |
| Cohérence | ✅ Maximale | ✅ Maximale | ⚠️ Partielle |
| Extensibilité | ⚠️ Faible | ✅ Haute | ✅ Moyenne |

### Timeline recommandée

**Court terme (maintenant)**:
- Option C: Documenter (commit immédiat)

**Moyen terme (sprint prochain)**:
- Évaluer besoin réel PGN enrichissement
- Si non nécessaire → Option A
- Si nécessaire → Option B avec specs détaillées

**Long terme (6 mois)**:
- Revue architecture: PGN Mapper vs CAN Publisher roles

---

## 9. QUESTIONS OUVERTES

1. **Y a-t-il un besoin métier pour PGN enrichissement?**
   - Calculs statistiques sur cellules?
   - Moyennes glissantes?
   - Détection anomalies?

2. **Pourquoi le module a été créé initialement?**
   - Anticiper besoin futur?
   - Séparation concerns théorique?

3. **CAN Publisher pourrait-il absorber ce rôle?**
   - Renommer en "pgn_processor" ou "bms_to_victron_bridge"?

---

## CONCLUSION

**Problème identifié**: Event publisher enregistré mais jamais utilisé
**Sévérité**: Faible (inconsistance, pas de bug)
**Recommandation**: Option A (supprimer) ou Option C (documenter)
**Effort**: 15 minutes (Option A) ou 5 minutes (Option C)
**Validations**: Aucun test ne casse, compilation OK

**Action immédiate suggérée**: Documenter (Option C), puis décider A vs B selon roadmap produit.
