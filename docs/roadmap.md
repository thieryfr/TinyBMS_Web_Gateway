# TinyBMS Web Gateway Roadmap

Ce document consolide le plan d'avancement du projet TinyBMS Web Gateway et sert de référence commune pour le suivi des travaux.

## Vue d'ensemble

Les quatre étapes principales sont enchaînées selon l'ordre ci-dessous. Les dépendances et charges estimées déterminent les dates cibles présentées. Chaque étape est décomposée en sous-tâches avec une équipe responsable clairement identifiée.

| Étape | Description | Équipe(s) responsable(s) | Date cible |
|-------|-------------|--------------------------|------------|
| 1 | Stabilisation firmware et intégration BMS | Firmware | 2024-07-12 |
| 2 | Validation fonctionnelle et tests d'endurance | Tests | 2024-07-26 |
| 3 | Documentation utilisateur et guide d'intégration | Docs | 2024-08-02 |
| 4 | Revue finale et préparation release | Firmware, Tests, Docs | 2024-08-09 |

## Détail des étapes et sous-tâches

### 1. Stabilisation firmware et intégration BMS (Firmware)
- Finaliser le support des profils TinyBMS existants.
- Mettre à jour la gestion des erreurs CAN/UART et les retransmissions critiques.
- Synchroniser la configuration avec les dernières révisions matérielles.

### 2. Validation fonctionnelle et tests d'endurance (Tests)
- Couvrir les cas nominaux et dégradés via la batterie de tests automatisés.
- Exécuter une campagne d'endurance 72h avec monitoring complet.
- Consolidation des rapports et analyse des régressions potentielles.

### 3. Documentation utilisateur et guide d'intégration (Docs)
- Rédiger le guide d'installation pas-à-pas (web + firmware).
- Décrire les scénarios d'intégration Victron et les paramètres recommandés.
- Mettre à jour les FAQ et le glossaire des alarmes.

### 4. Revue finale et préparation release (Firmware, Tests, Docs)
- Revue croisée des livrables firmware/tests/docs.
- Validation de la checklist de release et des artefacts associés.
- Go/No-Go meeting release et diffusion interne.

## Suivi Kanban

Le tableau Kanban interne (outil projet `Projects > TinyBMS Gateway`) reflète ces quatre colonnes dans l'ordre :
1. **Firmware – Stabilisation**
2. **Tests – Validation**
3. **Docs – Publication**
4. **Release – Go/No-Go**

Chaque carte est assignée à l'équipe correspondante et suit la progression décrite ci-dessus. Les dates cibles servent de jalons et conditionnent le passage d'une colonne à la suivante.

## Gestion des dépendances et charge

- La stabilisation firmware (2 semaines) est un prérequis direct pour lancer la validation fonctionnelle.
- Les tests d'endurance (2 semaines) peuvent débuter uniquement lorsque la branche firmware `feature/bms-stability` est gelée.
- La documentation (1 semaine) démarre après réception du rapport intermédiaire de tests.
- La revue finale (1 semaine) consolide les livrables et dépend de l'achèvement des trois étapes précédentes.

Les équipes sont responsables de mettre à jour l'état d'avancement hebdomadaire dans le tableau Kanban et de signaler tout risque impactant les dates cibles.
