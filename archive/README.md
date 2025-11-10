# 📦 Archives TinyBMS Gateway

Ce répertoire contient la documentation historique et obsolète du projet TinyBMS Gateway. Ces documents sont conservés à des fins de référence mais ne reflètent plus l'état actuel du projet.

**⚠️ AVERTISSEMENT :** Depuis la réorganisation de 2025, la documentation détaillée active est regroupée dans ce dossier (`archive/docs/`). Le répertoire racine `docs/` ne conserve plus que les artefacts nécessaires aux scripts (mappings CAN, en-têtes partagés). Consultez [`archive/docs/INDEX.md`](docs/INDEX.md) pour la table des matières complète.

> ℹ️ Les correspondances indiquées plus bas conservent les références d'origine vers `/docs/...` afin de tracer les anciens chemins. Ces documents se trouvent désormais sous `archive/docs/`.

---

## 📁 Structure des Archives

### 📚 reference/

Documents de référence historiques et plans de développement obsolètes.

#### Documents PHASE (Développement historique)

| Document | Taille | Description |
|----------|--------|-------------|
| **PHASE1_PR_DETAILS.md** | 8.3 KB | Détails PR Phase 1 - Initial setup |
| **PHASE3_PR_DETAILS.md** | 28 KB | Détails PR Phase 3 - Protocol implementation |
| **PHASE4_PR_DETAILS.md** | 27 KB | Détails PR Phase 4 - Advanced features |
| **PHASE4.5_PR_DETAILS.md** | 16 KB | Détails PR Phase 4.5 - Refinements |

**Raison d'archivage :** Historique de développement conservé pour référence. Le projet a évolué au-delà de ces phases.

#### Plans et Analyses Obsolètes

| Document | Description | Raison d'archivage |
|----------|-------------|-------------------|
| **FIXES_PLAN.md** | Plan de corrections de bugs | Corrections appliquées, voir `/docs/CORRECTIONS_APPLIED.md` |
| **PLAN_IMPLEMENTATION_CORRECTIONS.md** | Plan d'implémentation des corrections | Implémenté et intégré |
| **ANALYSIS_SUMMARY.txt** | Résumé d'analyse (format texte) | Remplacé par `/docs/SUMMARY_FR.md` |
| **README_DOCUMENTATION.md** | Ancien guide de documentation | Remplacé par `/docs/INDEX.md` |
| **UART_CORRECTIONS_IMPLEMENTATION.md** | Détails d'implémentation corrections UART | Corrections appliquées et intégrées |
| **PR_SUMMARY.md** | Résumé de PR historique | Document historique |
| **OPTIMIZATION_FONTS.md** | Optimisations polices (frontend) | Optimisations intégrées ou obsolètes |

---

### 📊 reports/

Rapports d'audit et d'expertise en français - référence historique de conformité.

| Document | Taille | Date | Description |
|----------|--------|------|-------------|
| **RAPPORT_ALIGNEMENT_FRONTEND_BACKEND.md** | 38 KB | Historique | Rapport d'alignement frontend/backend |
| **RAPPORT_AUDIT_FRONTEND_BACKEND.md** | 24 KB | Historique | Audit complet frontend/backend |
| **RAPPORT_EXPERTISE_INTERFACE_WEB.md** | 44 KB | Historique | Expertise de l'interface web |
| **RAPPORT_CONFORMITE.md** | 10 KB | Historique | Rapport de conformité générale |
| **RAPPORT_CONFORMITE_UART.md** | 17 KB | Historique | Rapport de conformité UART |

**Raison d'archivage :** Rapports de conformité historiques. Les audits récents et corrections appliquées sont dans `/docs/architecture/AUDIT_REPORT.md` et `/docs/CORRECTIONS_APPLIED.md`.

**📋 Contenu conservé pour :**
- Traçabilité des audits de conformité
- Référence historique des problèmes identifiés et résolus
- Documentation de la progression qualité du projet

---

### 📖 docs/

**54 fichiers** de documentation technique archivée.

#### Catégories principales :

##### 🏗️ Architecture (Obsolète)

| Document | Raison d'archivage |
|----------|-------------------|
| `architecture.md` | Remplacé par analyse détaillée dans `/docs/uart_can_analysis.md` et `/docs/architecture/` |
| `operations.md` | Procédures obsolètes, intégrées dans guides actuels |

##### 📡 Protocoles (Dépassés)

| Document | Raison d'archivage |
|----------|-------------------|
| `pgn_conversions.md` | Remplacé par `/docs/protocols/DOCUMENTATION_COMMUNICATIONS.md` |
| `tinybms_registers_300-343.md` | Mapping partiel, remplacé par mapping complet (59 registres) |
| `uart_bms_register_gap_analysis.md` | Gaps résolus, analyse obsolète |
| `can_35A_alarm_mapping.md` | Format obsolète, intégré dans documentation actuelle |
| `pgn_mapper_unused_event_publisher.md` | Code supprimé, référence obsolète |

##### 🧪 Tests (Anciens)

| Document | Raison d'archivage |
|----------|-------------------|
| `testing/validation_plan.md` | Procédures de test obsolètes |
| `testing/alarms.md` | Tests d'alarmes obsolètes |
| `testing/uart_can_bench.md` | Bench test obsolète |

##### 📋 Analyses Historiques

| Document | Raison d'archivage |
|----------|-------------------|
| `CHANGELOG.md` | Changelog historique (avant git tags) |
| `COHERENCE_REVIEW.md` | Revue de cohérence passée |
| `queue_size_correction_analysis.md` | Issue résolue |
| `mapping_audit.md` | Audit passé |
| `roadmap.md` | Roadmap obsolète |

##### 🔧 Références Modules (15 fichiers)

| Fichiers | Raison d'archivage |
|----------|-------------------|
| `reference/module_*.md` (15 fichiers) | Documentations détaillées des modules individuels, remplacées par code actuel et `/docs/architecture/FILES_REFERENCE.md` |

**Liste complète :**
- `module_alert_manager.md`
- `module_can_publisher.md`
- `module_can_victron.md`
- `module_config_manager.md`
- `module_event_bus.md`
- `module_monitoring.md`
- `module_mqtt_client.md`
- `module_mqtt_gateway.md`
- `module_ota_update.md`
- `module_pgn_mapper.md`
- `module_status_led.md`
- `module_system_control.md`
- `module_system_metrics.md`
- `module_uart_bms.md`
- `module_web_server.md`

---

## 📊 Statistiques des Archives

| Métrique | Valeur |
|----------|--------|
| **Total fichiers archivés** | 66 fichiers |
| **Documents PHASE** | 4 fichiers (79 KB) |
| **Rapports audit français** | 5 fichiers (133 KB) |
| **Documentation technique** | 54 fichiers |
| **Plans obsolètes** | 7 fichiers |

---

## 🔄 Migration vers Documentation Actuelle

Si vous consultez ces archives, voici comment trouver l'information équivalente dans la documentation actuelle :

### Mappings de Migration

| Archive | Document Actuel | Notes |
|---------|----------------|-------|
| `docs/architecture.md` | `/docs/uart_can_analysis.md` | Architecture détaillée avec 12 sections |
| `docs/operations.md` | `/docs/guides/INTEGRATION_GUIDE.md` | Procédures actualisées |
| `docs/pgn_conversions.md` | `/docs/protocols/DOCUMENTATION_COMMUNICATIONS.md` | Référence complète 59 registres + 19 PGN |
| `docs/api_endpoints.md` | `/web/API_REFERENCE.md` | Documentation API REST actuelle |
| `reference/module_*.md` | `/docs/architecture/FILES_REFERENCE.md` | Carte de navigation code |
| `FIXES_PLAN.md` | `/docs/CORRECTIONS_APPLIED.md` | Corrections appliquées |
| `README_DOCUMENTATION.md` | `/docs/INDEX.md` | Index structuré actuel |
| `UART_CORRECTIONS_IMPLEMENTATION.md` | `/docs/CORRECTIONS_APPLIED.md` | Détails des corrections |
| `OPTIMIZATION_FONTS.md` | `/web/` | Code source actuel |
| Rapports français | `/docs/architecture/AUDIT_REPORT.md` | Audit consolidé actuel |

---

## ⚠️ Utilisation de ces Archives

### ✅ Utilisations Appropriées

- Référence historique de l'évolution du projet
- Traçabilité des décisions d'architecture passées
- Comparaison avec l'état actuel pour comprendre l'évolution
- Consultation des rapports de conformité historiques

### ❌ Utilisations Inappropriées

- ❌ Ne PAS utiliser comme source de vérité technique actuelle
- ❌ Ne PAS baser des implémentations sur ces documents
- ❌ Ne PAS citer ces documents comme références dans le code
- ❌ Ne PAS les considérer comme documentation de référence

---

## 📝 Note de Conservation

Ces archives sont conservées pour :

1. **Traçabilité** : Comprendre les décisions historiques
2. **Conformité** : Référence des audits passés
3. **Historique** : Documentation de l'évolution du projet
4. **Référence** : Consultation ponctuelle de l'état passé

**Date d'archivage :** 2025-11-10
**Raison :** Réorganisation de la documentation pour refléter l'architecture et les implémentations actuelles

---

**Pour toute information actuelle, consultez :**
- **[`archive/docs/INDEX.md`](docs/INDEX.md)** - Index principal de la documentation archivée
- **[/README.md](../README.md)** - README principal du projet
