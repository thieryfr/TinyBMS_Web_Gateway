# 📚 TinyBMS Gateway - Index de Documentation

> **Dernière mise à jour :** 2025-11-10
> **Version du projet :** Voir [git tags](https://github.com/thieryfr/TinyBMS-GW/tags)

## 🎯 Navigation Rapide

| Profil | Document recommandé | Temps de lecture |
|--------|---------------------|------------------|
| 👔 **Manager / Chef de projet** | [SUMMARY_FR.md](SUMMARY_FR.md) | 10 min |
| 💻 **Développeur** | [QUICK_START.md](QUICK_START.md) → Section Développeur | 5 min |
| 🔍 **Reviewer / Auditeur** | [uart_can_analysis.md](uart_can_analysis.md) | 30 min |
| 🏗️ **Architecte** | [architecture/AUDIT_REPORT.md](architecture/AUDIT_REPORT.md) | 45 min |
| 🔌 **Intégrateur Victron** | [protocols/DOCUMENTATION_COMMUNICATIONS.md](protocols/DOCUMENTATION_COMMUNICATIONS.md) | 20 min |

---

## 📖 Documentation par Catégories

### 🚀 Guides de Démarrage

| Document | Description | Audience |
|----------|-------------|----------|
| [QUICK_START.md](QUICK_START.md) | Guides rapides par rôle (Manager/Dev/Reviewer) | Tous |
| [SUMMARY_FR.md](SUMMARY_FR.md) | Résumé exécutif en français (10 min) | Management |
| [README_ANALYSIS.md](README_ANALYSIS.md) | Guide de navigation des documents d'analyse | Développeurs |

---

### 🏗️ Architecture & Code Source

| Document | Description | Points clés |
|----------|-------------|------------|
| [architecture/AUDIT_REPORT.md](architecture/AUDIT_REPORT.md) | Rapport d'audit sécurité/conformité (29 KB) | 4 issues critiques identifiées |
| [architecture/FILES_REFERENCE.md](architecture/FILES_REFERENCE.md) | Carte de navigation du code source | Mapping rapide des fichiers |
| [uart_can_analysis.md](uart_can_analysis.md) | Analyse complète UART/CAN (1094 lignes) | 12 sections détaillées |
| [CORRECTIONS_APPLIED.md](CORRECTIONS_APPLIED.md) | Corrections appliquées (268 lignes) | 4 issues critiques corrigées |

**Architecture système :**
- 22 modules avec CMakeLists.txt indépendants
- 8 tâches FreeRTOS concurrentes
- Event bus avec 18 types d'événements
- ~17,000 lignes de code C/C++

---

### 🔌 Protocoles de Communication

| Document | Description | Protocoles couverts |
|----------|-------------|---------------------|
| [protocols/DOCUMENTATION_COMMUNICATIONS.md](protocols/DOCUMENTATION_COMMUNICATIONS.md) | Référence complète des protocoles (21 KB) | Modbus RTU, CAN Victron, REST API, WebSocket |
| [protocols/COMMUNICATION_REFERENCE.json](protocols/COMMUNICATION_REFERENCE.json) | Référence structurée JSON (14 KB) | Format machine-readable |
| [tinybms_register_can_flow.md](tinybms_register_can_flow.md) | Flux de données UART → CAN (120 lignes) | Conversion des registres |
| [interaction_diagrams.md](interaction_diagrams.md) | 8 diagrammes de séquence détaillés (661 lignes) | Flux temps réel |

**Protocoles implémentés :**

#### 🔧 UART/Modbus RTU (TinyBMS)
- **59 registres** pollés (45 adresses uniques : 0x0000-0x01F6)
- Polling : 250ms (configurable 100-1000ms)
- Timeout : 200ms
- Support wake-up en mode sleep
- Validation CRC

#### 🚗 Victron CAN Bus
- **19 PGN** (11-bit standard IDs : 0x305-0x382)
- Bitrate : 500 kbps
- GPIO : TX=7, RX=6
- Keepalive 0x305 @ 1000ms avec timeout 10s
- Compliance Victron validée

#### 🌐 REST API
- **15+ endpoints** (status, config, metrics, CAN, alerts, OTA)
- Rate limiting : 10 msg/sec
- Max payload : 32KB

#### 🔌 WebSocket
- **5 canaux** : telemetry, events, uart, can, alerts
- Telemetry @ 250ms
- Reconnexion automatique

#### 📡 MQTT
- Multiple hiérarchies de topics
- Pub/sub telemetry, alerts, status

---

### 📖 Guides d'Utilisation

| Document | Description | Cas d'usage |
|----------|-------------|-------------|
| [guides/INTEGRATION_GUIDE.md](guides/INTEGRATION_GUIDE.md) | Procédures d'intégration (20 KB) | Installation, configuration |
| [ota.md](ota.md) | Mise à jour firmware OTA (151 lignes) | Déploiement firmware |
| [monitoring_diagnostics.md](monitoring_diagnostics.md) | Diagnostics et monitoring (54 lignes) | Troubleshooting |
| [PR_DESCRIPTION.md](PR_DESCRIPTION.md) | Template PR avec corrections (205 lignes) | Contributions |

---

### 🖥️ Interface Web

| Document | Description |
|----------|-------------|
| [web/README.md](../web/README.md) | Vue d'ensemble interface web |
| [web/API_REFERENCE.md](../web/API_REFERENCE.md) | Documentation API REST |
| [web/TESTING.md](../web/TESTING.md) | Procédures de test frontend |
| [web/INTEGRATION_GUIDE.md](../web/INTEGRATION_GUIDE.md) | Instructions d'intégration |
| [web/BUG_ANALYSIS.md](../web/BUG_ANALYSIS.md) | Analyse des problèmes connus |

**Fonctionnalités :**
- Dashboard temps réel avec WebSocket
- 5 types de charts (battery, CAN, UART, energy)
- Mode sombre avec détection système
- PWA avec support offline
- i18n bilingue (FR/EN)
- Configuration management UI

---

### 🧪 Tests

| Type | Fichiers | Couverture |
|------|----------|------------|
| **Tests unitaires C/C++** | `test/test_*.c` | 12+ fichiers |
| **Tests frontend** | `web/test/tests/*.js` | Composants UI |
| **Tests UART** | `test/uart_test_vectors.c` | Vecteurs de test |
| **Tests CAN** | `test/test_can_*.c` | 3 fichiers |
| **Tests E2E** | `test/test_end_to_end.c` | Intégration complète |

---

### 🐛 Issues & Corrections

#### Issues Critiques Identifiées (4)

| Priorité | Issue | Fichier | Statut |
|----------|-------|---------|--------|
| 🔴 URGENT | CVL State Race Condition | `main/can_publisher/cvl_controller.c` | Documenté |
| 🔴 CRITICAL | Event Queue Too Small | `main/event_bus/event_bus.c` | Plan disponible |
| 🟠 HIGH | Mutex Timeout Too Short | `main/can_victron/can_victron.c` | Plan disponible |
| 🟠 HIGH | UART-CAN Tight Coupling | `main/uart_bms/uart_bms.cpp` | Architectural change requis |

Voir [uart_can_analysis.md](uart_can_analysis.md) pour détails complets.

---

### 📦 Archives & Historique

Les documents obsolètes ou historiques sont archivés dans :

- **[archive/reference/](../archive/reference/)** : Documents historiques
  - PHASE1-4.5_PR_DETAILS.md (4 fichiers)
  - Plans d'implémentation obsolètes
  - Analyses historiques

- **[archive/reports/](../archive/reports/)** : Rapports d'audit français
  - RAPPORT_ALIGNEMENT_FRONTEND_BACKEND.md
  - RAPPORT_AUDIT_FRONTEND_BACKEND.md
  - RAPPORT_EXPERTISE_INTERFACE_WEB.md
  - RAPPORT_CONFORMITE.md (2 fichiers)

- **[archive/docs/](../archive/docs/)** : 54 fichiers de documentation archivés
  - Ancienne architecture.md
  - Ancienne operations.md
  - Anciens documents de référence modules

---

## 📊 Statistiques du Projet

| Métrique | Valeur |
|----------|--------|
| **Lignes de code C/C++** | ~17,000 |
| **Modules** | 22 avec CMake indépendant |
| **Tâches FreeRTOS** | 8 concurrentes |
| **Registres UART** | 59 pollés (45 uniques) |
| **PGN CAN** | 19 messages |
| **Endpoints REST** | 15+ |
| **Canaux WebSocket** | 5 |
| **Types d'événements** | 18 définis |
| **Fichiers de tests** | 12+ |
| **Documentation active** | 26 fichiers |
| **Documentation archivée** | 54+ fichiers |

---

## 🔄 Commits Récents (Développement Actif)

1. **a76e3fb** - UART sleep mode wake-up + MODBUS tests
2. **aabb2d8** - Critical UART protocol compliance improvements
3. **33c439c** - Critical Victron CAN protocol compliance fixes
4. **706e8fe** - CAN references update (11-bit IDs @ 500kbps)
5. **4a6c9c5** - Use 11-bit Victron CAN identifiers

**Focus actuel :** Conformité protocoles, alignement frontend-backend, fiabilité UART

---

## 🔗 Liens Utiles

- **Matériel :** [ESP32-CAN-X2 Wiki](https://wiki.autosportlabs.com/ESP32-CAN-X2#Introduction)
- **ESP-IDF :** [Documentation officielle](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/get-started/)
- **Victron CAN :** Voir [protocols/DOCUMENTATION_COMMUNICATIONS.md](protocols/DOCUMENTATION_COMMUNICATIONS.md)

---

## 📝 Contribution

Pour contribuer au projet, consulter :
- [PR_DESCRIPTION.md](PR_DESCRIPTION.md) - Template de Pull Request
- [guides/INTEGRATION_GUIDE.md](guides/INTEGRATION_GUIDE.md) - Procédures d'intégration
- [CORRECTIONS_APPLIED.md](CORRECTIONS_APPLIED.md) - Historique des corrections

---

**Pour toute question :** Voir le document approprié ci-dessus ou consulter le README principal du projet.
