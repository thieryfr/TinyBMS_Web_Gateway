+# TinyBMS UART Request Alignment Plan
+
+Ce document propose des correctifs pour aligner les requêtes UART envoyées par ESP-CAN-X2 sur le protocole TinyBMS (Rev. D), sur la base de l'extrait de documentation fourni.
+
+## Hypothèses
+
+* L'adresse esclave TinyBMS est `0x07` pour les commandes binaires spécifiques (`0x07`, `0x09`, `0x0B`, `0x0D`).
+* Les champs `RL` et `PL` représentent respectivement la longueur en mots (read length) et le payload length (en octets).
+* Tous les paquets commencent par `0xAA` et se terminent par un CRC16 (LSB puis MSB) calculé sur la totalité de la trame.
+
+## Requêtes de lecture
+
+### Requête de polling unique (39 registres dispersés)
+
+Les données utilisées par `TinyBMS_Victron_Bridge` couvrent 39 registres répartis
+entre `0x0020` et `0x01F9`. Pour supprimer les six transactions Modbus
+actuelles, nous pouvons regrouper toutes les lectures dans **une seule** trame
+`Read Tiny BMS individual registers` (`0x09`).
+
+* Liste des registres (ordre d'émission LSB/MSB) :
+  `0x0020–0x0034`, `0x0066–0x0067`, `0x0071–0x0072`, `0x0131–0x0133`,
+  `0x013B–0x013F`, `0x01F4–0x01F9`.
+* Payload length `PL` = `39 * 2 = 0x4E` octets.
+* CRC16 (polynôme Modbus `0xA001`) calculé sur `AA 09 …` = `0x55BB`.
+
+**Trame complète prête à l'emploi :**
+
+```
+AA 09 4E
+ 20 00 21 00 22 00 23 00 24 00 25 00 26 00 27 00 28 00 29 00
+ 2A 00 2B 00 2C 00 2D 00 2E 00 2F 00 30 00 31 00 32 00 33 00
+ 34 00 66 00 67 00 71 00 72 00 31 01 32 01 33 01 3B 01 3C 01
+ 3D 01 3E 01 3F 01 F4 01 F5 01 F6 01 F7 01 F8 01 F9 01
+ BB 55
+```
+
+La réponse TinyBMS doit renvoyer `0xAA 0x09 <PL>` suivi de `78` octets de
+données (ordre LSB/MSB par registre) puis le CRC `0xBB 0x55`.
+
+### Lecture d'un bloc contigu (`0x07`)
+
+| Champ | Valeur | Commentaire |
+| --- | --- | --- |
+| Byte1 | `0xAA` | Preambule TinyBMS |
+| Byte2 | `0x07` | Code « Read registers block » |
+| Byte3 | `RL` | Nombre de registres 16 bits à lire |
+| Byte4 | `ADDR_L` | Adresse de départ (LSB) |
+| Byte5 | `ADDR_H` | Adresse de départ (MSB) |
+| Byte6 | `CRC_L` | CRC16 (LSB) |
+| Byte7 | `CRC_H` | CRC16 (MSB) |
+
+**Début de trame (hex) :** `AA 07 <RL> <ADDR_L> <ADDR_H>`
+
+### Lecture de registres individuels (`0x09`)
+
+| Champ | Valeur | Commentaire |
+| --- | --- | --- |
+| Byte1 | `0xAA` | Preambule |
+| Byte2 | `0x09` | Code « Read individual registers » |
+| Byte3 | `PL` | Longueur du payload en octets (`2 * n`) |
+| Byte4 | `ADDR1_L` | Adresse du premier registre (LSB) |
+| Byte5 | `ADDR1_H` | Adresse du premier registre (MSB) |
+| … | … | Adresses suivantes, 2 octets chacune |
+| Byte(n*2+4) | `CRC_L` | CRC16 LSB |
+| Byte(n*2+5) | `CRC_H` | CRC16 MSB |
+
+**Début de trame (hex) :** `AA 09 <PL> <ADDR1_L> <ADDR1_H> …`
+
+## Requêtes d'écriture
+
+### Lecture/écriture des registres de configuration
+
+L'éditeur de configuration s'appuyait sur des commandes ASCII (`:0001` /
+`:0101`). Pour se conformer au protocole binaire TinyBMS, il faut utiliser les
+commandes suivantes :
+
+* **Lecture d'un registre de configuration** – trame `0x07` avec `RL = 0x01`.
+  Exemple pour le registre `0x012C` (300 décimal, « Fully Charged Voltage ») :
+
+  ```
+  AA 07 01 2C 01 B1 AC
+  ```
+
+  (CRC = `0xACB1`). La réponse contiendra `0xAA 0x07 0x02` suivi de deux octets
+  little endian et du CRC.
+
+* **Écriture d'un registre de configuration** – trame `0x0D` (liste
+  adresse/valeur). Exemple pour écrire `0x0E42` (3650 mV) dans `0x012C` :
+
+  ```
+  AA 0D 04 2C 01 42 0E 09 E3
+  ```
+
+  (payload de 4 octets, CRC = `0xE309`). TinyBMS doit répondre `0xAA 0x01 0x00`
+  en cas de succès.
+
+* **Écriture simultanée de plusieurs registres contigus** – trame `0x0B`.
+  Exemple pour mettre à jour `0x012C` et `0x012D` en une opération :
+
+  ```
+  AA 0B 04 2C 01 42 0E A2 0C BE 96
+  ```
+
+  Ici `PL = 0x04` (deux registres), données little endian (`0x0E42`, `0x0CA2`),
+  CRC = `0x96BE`.
+
+Ces trames remplacent intégralement les commandes ASCII historiques et
+garantissent l'utilisation des opcodes UART TinyBMS documentés (`0x07`, `0x0B`,
+`0x0D`).
+
+### Écriture d'un bloc contigu (`0x0B`)
+
+| Champ | Valeur | Commentaire |
+| --- | --- | --- |
+| Byte1 | `0xAA` | Preambule |
+| Byte2 | `0x0B` | Code « Write registers block » |
+| Byte3 | `PL` | Longueur du payload en octets (`2 * n`) |
+| Byte4 | `ADDR_L` | Adresse de départ (LSB) |
+| Byte5 | `ADDR_H` | Adresse de départ (MSB) |
+| Byte6..n | `DATAx_L/DATAx_H` | Données little endian par registre |
+| Derniers octets | `CRC_L`, `CRC_H` | CRC16 |
+
+**Début de trame (hex) :** `AA 0B <PL> <ADDR_L> <ADDR_H> <DATA1_L> <DATA1_H> …`
+
+### Écriture de registres individuels (`0x0D`)
+
+| Champ | Valeur | Commentaire |
+| --- | --- | --- |
+| Byte1 | `0xAA` | Preambule |
+| Byte2 | `0x0D` | Code « Write individual registers » |
+| Byte3 | `PL` | Longueur du payload en octets (`4 * n`) |
+| Byte4 | `ADDR1_L` | Adresse du premier registre (LSB) |
+| Byte5 | `ADDR1_H` | Adresse du premier registre (MSB) |
+| Byte6 | `DATA1_L` | Valeur du premier registre (LSB) |
+| Byte7 | `DATA1_H` | Valeur du premier registre (MSB) |
+| … | … | Paire adresse/valeur répétée |
+| Derniers octets | `CRC_L`, `CRC_H` | CRC16 |
+
+**Début de trame (hex) :** `AA 0D <PL> <ADDR1_L> <ADDR1_H> <DATA1_L> <DATA1_H> …`
+
+## Mode compatible Modbus (page 6)
+
+### Lecture de bloc (compatible Modbus `0x03`)
+
+| Champ | Valeur | Commentaire |
+| --- | --- | --- |
+| Byte1 | `0xAA` | Preambule |
+| Byte2 | `0x03` | Code fonction Modbus |
+| Byte3 | `ADDR_H` | Adresse MSB |
+| Byte4 | `ADDR_L` | Adresse LSB |
+| Byte5 | `0x00` | High byte du nombre de registres |
+| Byte6 | `RL` | Nombre de registres à lire |
+| Byte7 | `CRC_L` | CRC16 LSB |
+| Byte8 | `CRC_H` | CRC16 MSB |
+
+**Début de trame (hex) :** `AA 03 <ADDR_H> <ADDR_L> 00 <RL>`
+
+### Écriture de bloc (compatible Modbus `0x10`)
+
+| Champ | Valeur | Commentaire |
+| --- | --- | --- |
+| Byte1 | `0xAA` | Preambule |
+| Byte2 | `0x10` | Code fonction Modbus |
+| Byte3 | `ADDR_H` | Adresse MSB |
+| Byte4 | `ADDR_L` | Adresse LSB |
+| Byte5 | `0x00` | High byte du nombre de registres |
+| Byte6 | `RL` | Nombre de registres à écrire |
+| Byte7 | `PL` | Nombre d'octets de données (`2 * n`) |
+| Byte8+ | `DATA1_H DATA1_L …` | Données big endian par registre |
+| Derniers octets | `CRC_L`, `CRC_H` | CRC16 |
+
+**Début de trame (hex) :** `AA 10 <ADDR_H> <ADDR_L> 00 <RL> <PL>`
+
+## Recommandations de mise en œuvre
+
+1. **Sélection des commandes :**
+   * Utiliser `0x07` lorsque l'on lit des plages contiguës (polling des blocs TinyBMS).
+   * Passer à `0x09` pour les lectures non contiguës (ex. agrégat de registres dispersés).
+   * Employer `0x0B` pour écrire un bloc contigu et `0x0D` pour des écritures ponctuelles réparties.
+   * Conserver `0x03`/`0x10` si une compatibilité Modbus est requise par des composants existants.
+2. **Encodage :**
+   * Adresses et données sont little endian pour les commandes natives (`0x07/0x09/0x0B/0x0D`).
+   * Les commandes compatibles Modbus (`0x03/0x10`) restent big endian pour adresse et données, conformément au standard Modbus.
+3. **Gestion CRC :**
+   * Calculer un CRC16 sur tous les octets, y compris `0xAA`, avant d'ajouter `CRC_L` et `CRC_H`.
+4. **Réponses attendues :**
+   * Vérifier le préambule `0xAA` et le code retour documenté (`0x01` pour ACK, `0x81` pour erreur) lors de l'analyse des réponses TinyBMS.
+
+Cette proposition sert de base à l’implémentation future afin d’assurer une conformité complète avec la documentation TinyBMS.
