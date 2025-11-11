# Archive - Documentation Historique

Ce dossier contient la documentation historique du projet TinyBMS-GW, incluant les rapports d'implémentation des différentes phases de développement et de refactoring.

## 📁 Structure

### Documentation Phase 1-4 (Implémentations)

- **PHASE1_IMPLEMENTATION.md** - Corrections bugs critiques, HTTPS/TLS, OTA signature
- **PHASE2_IMPLEMENTATION.md** - UART interrupt-driven, MQTTS, rate limiting
- **PHASE3_IMPLEMENTATION.md** - Documentation complète (ARCHITECTURE, DEVELOPMENT, MODULES)
- **PHASE4_IMPLEMENTATION.md** - Framework refactoring fichiers volumineux

### Documentation Phase 4 (Refactoring Détaillé)

- **PHASE4_REFACTORING_WEB_SERVER.md** - Découpage web_server.c (3507 → 820 lignes)
- **PHASE4_REFACTORING_CONFIG_MANAGER.md** - Découpage config_manager.c (2781 → 5 fichiers)
- **PHASE4_SUMMARY.md** - Récapitulatif complet Phase 4
- **REFACTORING_PLAN.md** - Plan technique détaillé du découpage

### Autres Documents

- **PR_DESCRIPTION.md** - Description Pull Request

### Documentation Historique (Pré-refactoring)

Ce dossier contient également toute la documentation d'analyse et de planification initiale du projet, incluant :

- Analyses de code
- Rapports de bugs
- Diagrammes d'architecture
- Protocoles de communication
- Guides de référence

## 📚 Documentation Active

La documentation de développement active (à jour avec le code refactoré) se trouve à la racine du projet :

- **README.md** - Description générale du projet
- **ARCHITECTURE.md** - Architecture système complète
- **DEVELOPMENT.md** - Guide développeur
- **MODULES.md** - Référence modules

## 🔍 Recherche

Pour rechercher dans l'archive :

```bash
# Rechercher un terme dans toute l'archive
grep -r "terme_recherché" archive/docs/

# Lister tous les fichiers markdown
find archive/docs/ -name "*.md" -type f

# Voir les phases d'implémentation
ls -lh archive/docs/PHASE*.md
```

## 📊 Statistiques

- **Phases complétées** : 4
- **Documentation archivée** : 9+ documents
- **Pages de documentation** : 140+ pages
- **Lignes refactorées** : 6288 lignes

---

**Note** : Cette archive est maintenue à des fins de référence historique. La documentation à jour se trouve à la racine du projet.
