# Audit de cohérence TinyBMS ↔ Victron CAN

Ce document synthétise les écarts entre l'implémentation actuelle (`main/can_publisher/conversion_table.c`) et la documentation de référence (`docs/pgn_conversions.md`, `README.md`) pour les PGN Victron supportés. Chaque ligne précise le statut de conformité, la formule utilisée côté firmware et l'attendu documentaire.

## Tableau comparatif « état actuel » vs « état cible »

| CAN ID / Registre | Champ | Implémentation actuelle (source) | Documentation (état cible) | Statut |
| --- | --- | --- | --- | --- |
| 0x18F351E5 (PGN 0x351) | CVL (bytes 0-1) | `encode_charge_limits()` encode `cvl_v` avec un facteur ×10 (0,1 V).【F:main/can_publisher/conversion_table.c†L444-L486】 | Section 0x351 annonce un facteur ×100 (0,01 V).【F:docs/pgn_conversions.md†L10-L17】 | À corriger (résolution doc ≠ code).
| 0x18F351E5 (PGN 0x351) | CCL / DCL (bytes 2-5) | Même fonction, facteur ×10 (0,1 A).【F:main/can_publisher/conversion_table.c†L444-L486】 | Facteur ×10 documenté (0,1 A).【F:docs/pgn_conversions.md†L14-L17】 | OK.
| 0x18F355E5 (PGN 0x355) | SOC / SOH (bytes 0-3) | `encode_soc_soh()` applique ×1 (1 %).【F:main/can_publisher/conversion_table.c†L491-L528】 | Documentation annonce ×10 (0,1 %).【F:docs/pgn_conversions.md†L19-L23】 | À corriger (résolution doc ≠ code).
| 0x18F355E5 (PGN 0x355) | SOC haute résolution (bytes 4-5) | Valeur optionnelle issue du registre 0x002E (×0,0001).【F:main/can_publisher/conversion_table.c†L507-L525】 | Non mentionné (la section 0x355 ne couvre que SOC/SOH).【F:docs/pgn_conversions.md†L19-L23】 | Manquant (documentation à compléter).
| 0x18F356E5 (PGN 0x356) | Tension / courant / température | `encode_voltage_current_temperature()` : tension ×100 (0,01 V), courant ×10 (0,1 A signé), température ×10 (0,1 °C).【F:main/can_publisher/conversion_table.c†L530-L551】 | Même échelles documentées.【F:docs/pgn_conversions.md†L25-L31】 | OK.
| 0x18F35AE5 (PGN 0x35A) | Tableau d'alarmes | Seuils et niveaux issus de `encode_alarm_status()` (tensions ±5 %, temp. 90 %, courants 80 %, déséquilibre 40/80 mV, réservés à `0b11`).【F:main/can_publisher/conversion_table.c†L553-L678】 | Table décrivant les mêmes seuils et bits, y compris l'usage de `low_temp_charge_cutoff_c`.【F:docs/pgn_conversions.md†L33-L68】 | OK.
| 0x18F35EE5 (PGN 0x35E) | Manufacturer string | `encode_manufacturer_string()` lit 0x01F4.. et replie sur `CONFIG_TINYBMS_CAN_MANUFACTURER`.【F:main/can_publisher/conversion_table.c†L760-L775】 | Documentation décrit la même logique (registres 0x01F4/0x01F5 ou constante).【F:docs/pgn_conversions.md†L70-L72】 | OK.
| 0x18F35FE5 (PGN 0x35F) | ID matériel / firmware / capacité / firmware interne | `encode_battery_identification()` assemble hardware/firmares, capacité (0,01 Ah) et firmware interne (0x01F6).【F:main/can_publisher/conversion_table.c†L240-L343】 | Description alignée sur les mêmes registres et échelles.【F:docs/pgn_conversions.md†L74-L80】 | OK.
| 0x18F371E5 (PGN 0x371) | Nom batterie (partie 2) | `encode_battery_name_part2()` lit 0x01F6.. ou `CONFIG_TINYBMS_CAN_BATTERY_NAME`.【F:main/can_publisher/conversion_table.c†L719-L725】 | Documentation identique.【F:docs/pgn_conversions.md†L70-L72】 | OK.
| 0x18F378E5 (PGN 0x378) | Compteurs d'énergie | `encode_energy_counters()` encode Wh/100 (0,1 kWh).【F:main/can_publisher/conversion_table.c†L706-L727】【F:main/can_publisher/conversion_table.c†L345-L393】 | Doc mentionne division par 100 et même résolution.【F:docs/pgn_conversions.md†L82-L86】 | OK.
| 0x18F379E5 (PGN 0x379) | Capacité installée | `encode_installed_capacity()` : Ah ×1 avec repli SOH / cellules.【F:main/can_publisher/conversion_table.c†L731-L757】 | Doc identique.【F:docs/pgn_conversions.md†L88-L91】 | OK.
| 0x18F382E5 (PGN 0x382) | Famille batterie | `encode_battery_family()` lit 0x01F8.. ou `CONFIG_TINYBMS_CAN_BATTERY_FAMILY`.【F:main/can_publisher/conversion_table.c†L726-L738】 | Doc identique.【F:docs/pgn_conversions.md†L93-L95】 | OK.
| README | Résumé des échelles PGN | Section « PGN Victron & conversions » annonce CVL en 0,01 V et SOC/SOH en 0,1 %.【F:README.md†L57-L66】 | Doit refléter l'implémentation effective. | À corriger (résumé incohérent avec le code).

## Impacts attendus si l'on aligne le code sur la documentation

- **CVL (0x351)** : passer de 0,1 V à 0,01 V multiplierait les valeurs CAN par 10. Les équipements Victron interpréteront une tension dix fois plus élevée si l'encodeur n'est pas ajusté, nécessitant une revalidation complète des limites charge/décharge et des tests d'interopérabilité.【F:main/can_publisher/conversion_table.c†L444-L486】
- **SOC/SOH (0x355)** : adopter 0,1 % au lieu de 1 % augmenterait la résolution mais casserait la rétrocompatibilité : les afficheurs Victron recevraient une valeur dix fois trop élevée tant que les conversions aval ne sont pas mises à jour. Cela impose la régénération des tests unitaires (`test/test_can_conversion.c`) et des jeux de captures CAN.【F:main/can_publisher/conversion_table.c†L491-L528】【F:test/test_can_conversion.c†L127-L197】
- **SOC haute résolution (0x355 bytes 4-5)** : documenter ce champ évite les incompréhensions côté intégration ; aucune modification de code n'est requise mais la feuille `pgn_mapping.xlsx` doit être complétée pour refléter les deux octets supplémentaires.
- **README** : corriger les facteurs évite de propager de mauvaises consignes aux intégrateurs et limite les tickets de support liés à une interprétation erronée des trames.【F:README.md†L57-L66】

## Plan de mise à jour de la documentation officielle

1. **Mettre à jour `docs/pgn_conversions.md`** : ajuster les facteurs CVL/SOC/SOH, décrire les octets 4-5 de 0x355 et référencer explicitement les fonctions `encode_charge_limits()` et `encode_soc_soh()` pour tracer les conversions.【F:docs/pgn_conversions.md†L10-L31】【F:main/can_publisher/conversion_table.c†L444-L528】
2. **Compléter `README.md`** : corriger les facteurs listés dans « PGN Victron & conversions TinyBMS » et ajouter un lien vers ce document d'audit pour garder la trace des décisions.【F:README.md†L57-L66】
3. **Synchroniser les supports internes** : mettre à jour `docs/pgn_mapping.xlsx` et `docs/testing/validation_plan.md` afin d'aligner les scénarios de test CAN sur les nouvelles échelles ou sur la documentation corrigée.【F:docs/pgn_conversions.md†L82-L86】【F:docs/testing/validation_plan.md†L1-L120】
4. **Communiquer les changements** : ajouter une entrée au `CHANGELOG.md` résumant l'alignement des échelles et informer l'équipe intégration Victron via la procédure décrite dans `docs/operations.md` (section validation).【F:docs/operations.md†L1-L120】

Une fois ces actions réalisées, relire `docs/architecture.md` pour vérifier si des schémas ou descriptions textuelles mentionnent les anciennes échelles et planifier leur mise à jour lors de la prochaine itération documentaire.【F:docs/architecture.md†L1-L36】
