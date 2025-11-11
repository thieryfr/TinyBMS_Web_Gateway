# Exigences de sécurité réseau et procédures OTA

## 1. Exigences de sécurité réseau

### 1.1 Segmentation et topologie
- Isoler le TinyBMS-GW dans un VLAN dédié aux équipements industriels.
- Bloquer tout trafic entrant non explicitement autorisé depuis les réseaux bureautiques.
- Limiter les communications sortantes aux seules destinations nécessaires (serveurs d'API et de supervision).

### 1.2 Authentification et chiffrement
- Activer WPA2-Enterprise ou WPA3 pour le Wi-Fi lorsque possible ; à défaut, WPA2-PSK avec mot de passe robuste (>16 caractères) renouvelé trimestriellement.
- Utiliser TLS 1.2 minimum pour toutes les connexions HTTP(s) sortantes.
- Renouveler les certificats clients avant expiration et révoquer ceux compromis via la PKI interne.

### 1.3 Gestion des accès
- Restreindre l'accès SSH aux seules clés publiques approuvées et limiter les comptes administrateurs.
- Désactiver tout compte inutilisé et appliquer une rotation de clés au moins tous les 6 mois.
- Journaliser les connexions et échecs d'authentification (syslog distant recommandé).

### 1.4 Mise à jour des dépendances
- Vérifier mensuellement les bulletins de sécurité pour l'ESP-IDF et les bibliothèques réseau.
- Appliquer les correctifs critiques dans un délai de 14 jours.
- Documenter chaque mise à jour dans le registre de maintenance.

## 2. Procédures OTA

### 2.1 Préparation du paquet
- Générer l'image firmware via la pipeline CI signée avec la clé privée OTA.
- Vérifier la somme SHA-256 et le numéro de version avant publication.
- Définir un identifiant de build unique et l'enregistrer dans le serveur OTA.

### 2.2 Fenêtre de déploiement
- Planifier les mises à jour hors des heures de pointe de supervision.
- Prévenir les intégrateurs 48 h à l'avance avec les notes de version correspondantes.
- Prévoir un accès de secours (USB ou JTAG) en cas d'échec critique.

### 2.3 Déroulé opérationnel
1. Sauvegarder la configuration actuelle (fichiers `sdkconfig` spécifiques et paramètres réseau).
2. Déclencher le téléchargement OTA depuis l'API de gestion ou via la commande CLI `ota_update --url <package>`.
3. Surveiller les journaux systèmes pour confirmer la progression (`OTA_START`, `OTA_PROGRESS`, `OTA_SUCCESS`).
4. Après redémarrage, valider la version en interrogeant l'API `/system/info`.
5. Documenter la mise à jour (heure, version, opérateur) dans le registre.

### 2.4 Plan de retour arrière
- Conserver la dernière version stable dans la partition OTA_0 pour retour automatique.
- En cas d'échec répété (>2 tentatives), verrouiller l'OTA et déclencher un diagnostic manuel.
- Restaurer la configuration depuis la sauvegarde initiale et reprogrammer via le port série si nécessaire.

### 2.5 Conformité et audit
- Archiver les journaux OTA pendant 12 mois pour audit.
- Réaliser un test OTA trimestriel sur banc de validation.
- Mettre à jour cette procédure après chaque revue de sécurité.
