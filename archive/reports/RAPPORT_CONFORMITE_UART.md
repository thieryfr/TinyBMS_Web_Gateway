# Rapport d'Analyse de Conformité UART TinyBMS
## TinyBMS Communication Protocols Rev D (2025-07-04)

**Date d'analyse**: 2025-11-10
**Analysé par**: Claude
**Projet**: TinyBMS-GW

---

## Table des matières

1. [Résumé Exécutif](#résumé-exécutif)
2. [Configuration UART](#configuration-uart)
3. [Implémentation du Protocole](#implémentation-du-protocole)
4. [Problèmes de Conformité Identifiés](#problèmes-de-conformité-identifiés)
5. [Recommandations](#recommandations)
6. [Plan de Correction](#plan-de-correction)

---

## 1. Résumé Exécutif

### Points Conformes ✓

- **Configuration UART** : 115200 baud, 8N1, no flow control
- **CRC16** : Implémentation correcte (MODBUS polynomial 0xA001, init 0xFFFF)
- **Format des trames** : Préambule 0xAA, structure conforme
- **Commandes implémentées** : Format correct pour 0x07, 0x09, 0x0D
- **Ordre des bytes** : LSB first conforme
- **Validation des trames** : CRC, preamble, payload length

### Problèmes Critiques ❌

1. **Support MODBUS incomplet** : Commandes 0x03 et 0x10 non implémentées
2. **Commandes propriétaires manquantes** : 20 commandes sur 23 non implémentées
3. **Gestion du Sleep Mode** : Pas de double-envoi lors du wake-up
4. **Multi-packet** : Pas de support pour les réponses multi-packets

### Score de Conformité

- **Configuration de base** : 100% ✓
- **Commandes implémentées** : 13% (3/23)
- **Fonctionnalités MODBUS** : 0% (0/2)
- **Gestion avancée** : 50%

**Score Global** : **41% de conformité**

---

## 2. Configuration UART

### 2.1 Spécifications Documentées (p.4)

```
Baudrate: 115200 bit/s
Data bits: 8
Stop bits: 1
Parity: None
Flow control: None
```

### 2.2 Implémentation (uart_bms.cpp:653-660)

```cpp
uart_config_t config = {
    .baud_rate = UART_BMS_BAUD_RATE,        // 115200 ✓
    .data_bits = UART_DATA_8_BITS,          // 8 bits ✓
    .parity = UART_PARITY_DISABLE,          // No parity ✓
    .stop_bits = UART_STOP_BITS_1,          // 1 stop bit ✓
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,  // No flow control ✓
    .source_clk = UART_SCLK_APB,
};
```

**Verdict** : ✓ **100% CONFORME**

---

## 3. Implémentation du Protocole

### 3.1 CRC16 Checksum

#### Spécifications (p.11-12)

- Polynomial: x¹⁶+x¹⁵+x²+1 (0x8005 in HEX format)
- Reflected: 0xA001
- Initial value: 0xFFFF
- Toutes les commandes doivent contenir un CRC 16 bits

#### Implémentation (uart_frame_builder.cpp:16-34)

```cpp
uint16_t uart_frame_builder_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;  // ✓ Init correct
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;  // ✓ Polynomial correct
            } else {
                crc = crc >> 1;
            }
        }
    }
    return crc;
}
```

**Verdict** : ✓ **CONFORME** - Implémentation standard MODBUS CRC

### 3.2 Commandes Implémentées

#### 3.2.1 Read Individual Registers (0x09)

**Documentation** : Section 1.1.3 (p.5)

```
Request:  0xAA 0x09 PL ADDR1:LSB ADDR1:MSB ... ADDRn:LSB ADDRn:MSB CRC:LSB CRC:MSB
Response: 0xAA 0x09 PL ADDR1:LSB ADDR1:MSB DATA1:LSB DATA1:MSB ... CRC:LSB CRC:MSB
```

**Implémentation** : uart_frame_builder.cpp:36-70

```cpp
buffer[offset++] = kTinyBmsPreamble;              // 0xAA ✓
buffer[offset++] = kTinyBmsOpcodeReadIndividual;  // 0x09 ✓
buffer[offset++] = static_cast<uint8_t>(payload_length);  // PL ✓
for (size_t i = 0; i < UART_BMS_REGISTER_WORD_COUNT; ++i) {
    const uint16_t address = g_uart_bms_poll_addresses[i];
    buffer[offset++] = static_cast<uint8_t>(address & 0xFF);        // LSB ✓
    buffer[offset++] = static_cast<uint8_t>((address >> 8) & 0xFF);  // MSB ✓
}
// CRC LSB/MSB ✓
```

**Verdict** : ✓ **CONFORME**

**Note** : Le payload est de 118 bytes (59 registres × 2 bytes), ce qui respecte la limite de 127 bytes pour un single-packet (bit 7 = 0)

#### 3.2.2 Write Individual Registers (0x0D)

**Documentation** : Section 1.1.5 (p.5-6)

```
Request: 0xAA 0x0D PL ADDR1:LSB ADDR1:MSB DATA1:LSB DATA1:MSB ... CRC:LSB CRC:MSB
```

**Implémentation** : uart_frame_builder.cpp:72-106

```cpp
buffer[offset++] = kTinyBmsPreamble;               // 0xAA ✓
buffer[offset++] = kTinyBmsOpcodeWriteIndividual;  // 0x0D ✓
buffer[offset++] = static_cast<uint8_t>(payload_length);  // 4 bytes ✓
buffer[offset++] = static_cast<uint8_t>(address & 0xFF);  // ADDR LSB ✓
buffer[offset++] = static_cast<uint8_t>((address >> 8) & 0xFF);  // ADDR MSB ✓
buffer[offset++] = static_cast<uint8_t>(value & 0xFF);  // VALUE LSB ✓
buffer[offset++] = static_cast<uint8_t>((value >> 8) & 0xFF);  // VALUE MSB ✓
```

**Verdict** : ✓ **CONFORME**

#### 3.2.3 Read Register Block (0x07)

**Documentation** : Section 1.1.2 (p.4)

```
Request:  0xAA 0x07 RL ADDR:LSB ADDR:MSB CRC:LSB CRC:MSB
Response: 0xAA 0x07 PL DATA1:LSB DATA1:MSB ... DATAn:LSB DATAn:MSB CRC:LSB CRC:MSB
```

**Implémentation** : uart_frame_builder.cpp:108-138

```cpp
buffer[offset++] = kTinyBmsPreamble;          // 0xAA ✓
buffer[offset++] = kTinyBmsOpcodeReadBlock;   // 0x07 ✓
buffer[offset++] = 0x01;  // RL = 1 register ✓
buffer[offset++] = static_cast<uint8_t>(address & 0xFF);  // ADDR LSB ✓
buffer[offset++] = static_cast<uint8_t>((address >> 8) & 0xFF);  // ADDR MSB ✓
```

**Verdict** : ✓ **CONFORME**

### 3.3 Response Parser

**Validation** : uart_response_parser.cpp:133-173

```cpp
// Vérifications conformes :
✓ Preamble 0xAA
✓ Opcode 0x09
✓ Payload length pair (multiple de 2)
✓ CRC LSB puis MSB
✓ Taille de trame valide
```

**Verdict** : ✓ **CONFORME**

---

## 4. Problèmes de Conformité Identifiés

### 4.1 Commandes MODBUS Non Implémentées

#### Problème

La documentation (p.4) spécifie :

> "Various proprietary commands are available for fast communication, also **MODBUS commands 03 and 16** are supported for rapid integration to existing industrial systems."

**Commandes manquantes** :
- **0x03** : Read Holding Registers (MODBUS compatible) - Section 1.1.6 (p.6)
- **0x10** : Write Multiple Registers (MODBUS compatible) - Section 1.1.7 (p.6)

#### Impact

- ❌ **Non-conformité avec les spécifications**
- ⚠️ **Incompatibilité avec les systèmes MODBUS standard**
- ⚠️ **Perte de l'avantage "rapid integration to existing industrial systems"**

#### Format MODBUS 0x03 (Read Holding Registers)

```
Request:  0xAA 0x03 ADDR:MSB ADDR:LSB 0x00 RL CRC:LSB CRC:MSB
Response: 0xAA 0x03 PL DATA1:MSB DATA1:LSB ... DATAn:MSB DATAn:LSB CRC:LSB CRC:MSB
```

**Note** : Ordre des bytes inversé (MSB first) par rapport aux commandes propriétaires!

#### Format MODBUS 0x10 (Write Multiple Registers)

```
Request: 0xAA 0x10 ADDR:MSB ADDR:LSB 0x00 RL PL DATA1:MSB DATA1:LSB ... CRC:LSB CRC:MSB
Response: 0xAA 0x10 ADDR:MSB ADDR:LSB 0x00 RL CRC:LSB CRC:MSB
```

### 4.2 Commandes Propriétaires Manquantes

#### Liste des commandes non implémentées

| Opcode | Commande | Section | Priorité |
|--------|----------|---------|----------|
| 0x02 | Reset BMS, clear Events and Statistics | 1.1.8 | ⚠️ Moyenne |
| 0x0B | Write Tiny BMS registers block | 1.1.4 | ⚠️ Moyenne |
| 0x11 | Read Tiny BMS newest Events | 1.1.9 | 🔴 Haute |
| 0x12 | Read Tiny BMS all Events | 1.1.10 | ⚠️ Moyenne |
| 0x14 | Read battery pack voltage | 1.1.11 | ⚠️ Faible |
| 0x15 | Read battery pack current | 1.1.12 | ⚠️ Faible |
| 0x16 | Read max cell voltage | 1.1.13 | ⚠️ Faible |
| 0x17 | Read min cell voltage | 1.1.14 | ⚠️ Faible |
| 0x18 | Read online status | 1.1.15 | ⚠️ Moyenne |
| 0x19 | Read lifetime counter | 1.1.16 | ⚠️ Faible |
| 0x1A | Read SOC value | 1.1.17 | ⚠️ Faible |
| 0x1B | Read device temperatures | 1.1.18 | ⚠️ Faible |
| 0x1C | Read cells voltages | 1.1.19 | ⚠️ Faible |
| 0x1D | Read settings values | 1.1.20 | ⚠️ Moyenne |
| 0x1E | Read version | 1.1.21 | ⚠️ Moyenne |
| 0x1F | Read extended version | 1.1.22 | ⚠️ Moyenne |
| 0x20 | Read speed/distance/time | 1.1.23 | ⚠️ Faible |

#### Impact

La plupart de ces commandes sont des **raccourcis** pour lire des données spécifiques. Elles sont **techniquement redondantes** car les mêmes données peuvent être obtenues via les commandes 0x07 ou 0x09.

**Priorité** : Faible à Moyenne (sauf 0x11 pour Events qui est importante)

### 4.3 Gestion du Sleep Mode

#### Problème

**Documentation** (p.12, section 1.3) :

> "**Note**: If Tiny BMS device is in sleep mode, the first command must be send twice. After received the first command BMS wakes up from sleep mode, but the response to the command will be sent when it receives the command a second time."

**État actuel** : Aucune gestion du sleep mode détectée dans le code.

#### Impact

- ⚠️ **Échecs de communication possibles** après période d'inactivité
- ⚠️ **Perte de la première trame** après wake-up
- ⚠️ **Timeout inutiles** lors du réveil du BMS

#### Solution Requise

Implémenter une logique de retry/double-send :

```cpp
esp_err_t uart_bms_send_with_wakeup(const uint8_t* frame, size_t length)
{
    // Premier envoi (peut-être ignoré si BMS en sleep)
    uart_write_bytes(UART_BMS_UART_PORT, frame, length);
    vTaskDelay(pdMS_TO_TICKS(50));  // Attente wake-up

    // Second envoi (BMS doit être éveillé maintenant)
    uart_write_bytes(UART_BMS_UART_PORT, frame, length);

    // Attendre réponse
    return uart_bms_wait_response();
}
```

### 4.4 Multi-Packet Support

#### Problème

**Documentation** (p.5, section 1.1.3) :

Le byte **PL (Payload Length)** a un format spécial :

```
Bit 7 | Bit 6 | Bit 5 | Bit 4 | Bit 3 | Bit 2 | Bit 1 | Bit 0
------+-------+-------+-------+-------+-------+-------+------
  0   |           Payload size in bytes (last packet)
  1   |                 Current packet ID
```

**État actuel** : Le code assume toujours un single-packet (bit 7 = 0)

#### Impact

- ⚠️ **Limitation à 127 bytes** de payload par commande
- ⚠️ **Impossible de lire** > 63 registres d'un coup
- ⚠️ **Limitation actuelle acceptable** : 59 registres × 2 bytes = 118 bytes < 127

**Note** : Ce n'est pas un problème immédiat car le polling actuel (59 registres) tient en un paquet.

### 4.5 Payload Length pour Multi-Registers

#### Observation

Dans `uart_frame_builder.cpp`, le calcul du payload pour Read Individual est correct :

```cpp
const size_t payload_length = UART_BMS_REGISTER_WORD_COUNT * sizeof(uint16_t);
```

Cela donne 118 bytes, ce qui est **inférieur à 128**, donc le bit 7 sera automatiquement 0.

**Verdict** : ✓ **Conforme pour l'utilisation actuelle**

---

## 5. Recommandations

### 5.1 Priorité Haute 🔴

#### 5.1.1 Implémenter le Support MODBUS (0x03, 0x10)

**Raison** : Conformité avec la documentation, interopérabilité MODBUS

**Effort** : Moyen (2-3 heures)

**Fichiers à modifier** :
- `main/uart_bms/uart_frame_builder.h` : Ajouter prototypes
- `main/uart_bms/uart_frame_builder.cpp` : Implémenter builders
- `main/uart_bms/uart_response_parser.cpp` : Parser les réponses MODBUS

**Attention** : Les commandes MODBUS utilisent **MSB first** contrairement aux commandes propriétaires (LSB first)

#### 5.1.2 Implémenter la Gestion du Sleep Mode

**Raison** : Fiabilité de la communication après inactivité

**Effort** : Faible (1 heure)

**Fichiers à modifier** :
- `main/uart_bms/uart_bms.cpp` : Ajouter logique de double-send

**Implémentation suggérée** :

```cpp
static esp_err_t uart_bms_send_with_retry(const uint8_t* frame,
                                          size_t length,
                                          uint8_t* response,
                                          size_t response_size,
                                          size_t* response_length,
                                          uint32_t timeout_ms)
{
    // Premier envoi (wake-up si nécessaire)
    int written = uart_write_bytes(UART_BMS_UART_PORT, frame, length);
    if (written != length) {
        return ESP_ERR_TIMEOUT;
    }

    // Attendre un peu pour le wake-up
    vTaskDelay(pdMS_TO_TICKS(50));

    // Essayer de recevoir la réponse
    int len = uart_read_bytes(UART_BMS_UART_PORT,
                             response,
                             response_size,
                             pdMS_TO_TICKS(timeout_ms));

    // Si pas de réponse, renvoyer (BMS était peut-être en sleep)
    if (len <= 0) {
        ESP_LOGD(kTag, "No response, retrying (possible wake-up needed)");

        written = uart_write_bytes(UART_BMS_UART_PORT, frame, length);
        if (written != length) {
            return ESP_ERR_TIMEOUT;
        }

        len = uart_read_bytes(UART_BMS_UART_PORT,
                             response,
                             response_size,
                             pdMS_TO_TICKS(timeout_ms));
    }

    if (len > 0) {
        *response_length = len;
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}
```

#### 5.1.3 Implémenter Read Events (0x11)

**Raison** : Diagnostic et monitoring des erreurs/warnings du BMS

**Effort** : Moyen (2 heures)

**Utilité** : Critique pour la supervision du système

### 5.2 Priorité Moyenne ⚠️

#### 5.2.1 Implémenter Write Block (0x0B)

**Raison** : Configuration multiple de registres efficace

**Effort** : Faible (1 heure)

#### 5.2.2 Implémenter Reset/Clear (0x02)

**Raison** : Gestion des events et statistiques

**Effort** : Faible (30 minutes)

#### 5.2.3 Implémenter Read Settings (0x1D)

**Raison** : Lecture des valeurs min/max/default/current

**Effort** : Moyen (1 heure)

### 5.3 Priorité Faible 📘

Les commandes 0x14-0x1C, 0x1E-0x20 sont des **raccourcis** optionnels. Elles peuvent être implémentées pour :
- **Optimisation** : Réduction du payload
- **Compatibilité** : Avec des outils existants TinyBMS

**Effort total** : 4-5 heures

---

## 6. Plan de Correction

### Phase 1 : Corrections Critiques (Priorité Haute)

**Durée estimée** : 1 journée

1. ✅ Implémenter support MODBUS 0x03 (Read Holding Registers)
2. ✅ Implémenter support MODBUS 0x10 (Write Multiple Registers)
3. ✅ Ajouter gestion du sleep mode (double-send)
4. ✅ Implémenter Read Events (0x11)
5. ✅ Tests unitaires et validation

### Phase 2 : Améliorations (Priorité Moyenne)

**Durée estimée** : 0.5 journée

1. ⚠️ Implémenter Write Block (0x0B)
2. ⚠️ Implémenter Reset (0x02)
3. ⚠️ Implémenter Read Settings (0x1D)
4. ⚠️ Tests d'intégration

### Phase 3 : Complétion (Priorité Faible)

**Durée estimée** : 0.5 journée (optionnel)

1. 📘 Implémenter commandes de raccourci (0x14-0x1C, 0x1E-0x20)
2. 📘 Optimiser le polling si nécessaire
3. 📘 Documentation complète

### Tests de Conformité

Après chaque phase, valider :

1. **Tests Unitaires** :
   - CRC correct pour toutes les commandes
   - Format des trames conforme
   - Validation des réponses

2. **Tests d'Intégration** :
   - Communication avec un TinyBMS réel
   - Gestion du sleep mode
   - Lecture/écriture de registres

3. **Tests de Non-Régression** :
   - Les commandes existantes fonctionnent toujours
   - Pas de degradation de performance

---

## Annexes

### A. Tableau Récapitulatif des Commandes

| Opcode | Nom | Implémenté | Priorité | Section |
|--------|-----|------------|----------|---------|
| 0x00 | ACK/NACK | ✓ | - | 1.1.1 |
| 0x02 | Reset/Clear | ❌ | Moyenne | 1.1.8 |
| 0x03 | MODBUS Read | ❌ | **Haute** | 1.1.6 |
| 0x07 | Read Block | ✓ | - | 1.1.2 |
| 0x09 | Read Individual | ✓ | - | 1.1.3 |
| 0x0B | Write Block | ❌ | Moyenne | 1.1.4 |
| 0x0D | Write Individual | ✓ | - | 1.1.5 |
| 0x10 | MODBUS Write | ❌ | **Haute** | 1.1.7 |
| 0x11 | Read Newest Events | ❌ | **Haute** | 1.1.9 |
| 0x12 | Read All Events | ❌ | Moyenne | 1.1.10 |
| 0x14-0x20 | Raccourcis | ❌ | Faible | 1.1.11-23 |

### B. Références

- **Documentation** : TinyBMS Communication Protocols Rev D (2025-07-04)
- **Fichiers analysés** :
  - `main/uart_bms/uart_bms_protocol.h`
  - `main/uart_bms/uart_bms_protocol.c`
  - `main/uart_bms/uart_frame_builder.h`
  - `main/uart_bms/uart_frame_builder.cpp`
  - `main/uart_bms/uart_response_parser.h`
  - `main/uart_bms/uart_response_parser.cpp`
  - `main/uart_bms/uart_bms.h`
  - `main/uart_bms/uart_bms.cpp`

### C. Contact

Pour toute question sur ce rapport, contacter l'équipe de développement.

---

**Fin du Rapport d'Analyse de Conformité UART**
