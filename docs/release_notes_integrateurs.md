# Notes de version TinyBMS-GW

## Version 1.6.0 (2024-05)

### Nouveautés
- Renforcement des politiques de sécurité réseau : prise en charge WPA3 et rotation automatique des clés SSH.
- Ajout de la surveillance des journaux OTA et intégration au serveur syslog central.
- Automatisation des tests API critiques dans la pipeline CI.

### Correctifs
- Résolution des erreurs de reconnexion Wi-Fi lors des coupures prolongées.
- Correction d'un dépassement de tampon dans le module MQTT lors de charges élevées.
- Harmonisation des réponses d'erreur de l'API `/battery/status`.

### Impacts pour les intégrateurs
- Mise à jour requise des paramètres réseau dans les environnements de préproduction (nouvelles exigences VLAN et TLS).
- Nécessité de déployer la nouvelle checklist de validation avant livraison terrain.
- Prévoir une session de mise à jour OTA contrôlée pour activer les nouvelles fonctionnalités.

### Actions recommandées
1. Télécharger le firmware `tinybms-gw-1.6.0.bin` et vérifier la signature fournie.
2. Mettre à jour les configurations de sécurité selon le document « Exigences de sécurité réseau et procédures OTA ».
3. Intégrer la checklist de validation dans vos procédures CI/QA.
4. Planifier une fenêtre de maintenance pour appliquer la mise à jour OTA et valider les tests post-déploiement.

### Compatibilité
- Compatible avec les cartes TinyBMS-GW révision B et ultérieure.
- Non rétrocompatible avec les déploiements utilisant TLS 1.0/1.1.

### Support
Pour toute question, contacter support@tinybms.example ou consulter la base de connaissances interne.
