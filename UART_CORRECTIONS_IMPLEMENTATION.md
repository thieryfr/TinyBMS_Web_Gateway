# Plan d'Implémentation des Corrections UART

## Vue d'Ensemble

Ce document détaille l'implémentation des corrections pour atteindre la conformité complète avec la documentation TinyBMS Communication Protocols Rev D.

---

## Correction 1 : Support MODBUS 0x03 (Read Holding Registers)

### Spécifications

**Section** : 1.1.6 (p.6)

**Format de requête** :
```
Byte 1 | Byte 2 | Byte 3     | Byte 4     | Byte 5 | Byte 6 | Byte 7  | Byte 8
0xAA   | 0x03   | ADDR1:MSB  | ADDR1:LSB  | 0x00   | RL     | CRC:LSB | CRC:MSB
```

**Format de réponse** :
```
Byte 1 | Byte 2 | Byte 3 | Byte 4      | Byte 5      | ...  | Byte n*2+2  | Byte n*2+3  | Byte n*2+4 | Byte n*2+5
0xAA   | 0x03   | PL     | DATA1:MSB   | DATA1:LSB   | ...  | DATAn:MSB   | DATAn:LSB   | CRC:LSB    | CRC:MSB
```

**⚠️ Attention** : MODBUS utilise **MSB first** (Big Endian) contrairement aux commandes propriétaires!

### Implémentation

#### Fichier : `main/uart_bms/uart_frame_builder.h`

```cpp
/**
 * @brief Build a MODBUS Read Holding Registers request (0x03)
 *
 * @param buffer Destination buffer
 * @param buffer_size Buffer capacity
 * @param start_address Starting register address (MODBUS format: MSB first)
 * @param register_count Number of registers to read (max 127)
 * @param out_length Resulting frame length
 * @return ESP_OK on success
 */
esp_err_t uart_frame_builder_build_modbus_read(uint8_t *buffer,
                                               size_t buffer_size,
                                               uint16_t start_address,
                                               uint8_t register_count,
                                               size_t *out_length);
```

#### Fichier : `main/uart_bms/uart_frame_builder.cpp`

```cpp
esp_err_t uart_frame_builder_build_modbus_read(uint8_t *buffer,
                                               size_t buffer_size,
                                               uint16_t start_address,
                                               uint8_t register_count,
                                               size_t *out_length)
{
    if (buffer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    // Format: AA 03 ADDR:MSB ADDR:LSB 0x00 RL CRC:LSB CRC:MSB
    constexpr size_t kModbusReadFrameSize = 8;
    if (buffer_size < kModbusReadFrameSize) {
        return ESP_ERR_INVALID_SIZE;
    }

    // Limiter à 127 registres (max pour single packet)
    if (register_count == 0 || register_count > 0x7F) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t offset = 0;
    buffer[offset++] = 0xAA;  // Preamble
    buffer[offset++] = 0x03;  // MODBUS Read Holding Registers
    buffer[offset++] = static_cast<uint8_t>((start_address >> 8) & 0xFF);  // ADDR MSB
    buffer[offset++] = static_cast<uint8_t>(start_address & 0xFF);  // ADDR LSB
    buffer[offset++] = 0x00;  // Reserved
    buffer[offset++] = register_count;  // RL

    const uint16_t crc = uart_frame_builder_crc16(buffer, offset);
    buffer[offset++] = static_cast<uint8_t>(crc & 0xFF);  // CRC LSB
    buffer[offset++] = static_cast<uint8_t>((crc >> 8) & 0xFF);  // CRC MSB

    if (out_length != nullptr) {
        *out_length = offset;
    }

    return ESP_OK;
}
```

#### Fichier : `main/uart_bms/uart_response_parser.cpp`

Ajouter la validation des réponses MODBUS :

```cpp
esp_err_t UartResponseParser::validateModbusFrame(const uint8_t* frame,
                                                   size_t length,
                                                   size_t* register_count) const
{
    if (frame == nullptr || register_count == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    // Minimum: AA 03 PL DATA CRC (6 bytes minimum)
    if (length < 6) {
        return ESP_ERR_INVALID_SIZE;
    }

    // Vérifier preamble et opcode
    if (frame[0] != 0xAA || frame[1] != 0x03) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t payload_len = frame[2];
    if ((payload_len % 2) != 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t expected_len = 3 + payload_len + 2;  // header + payload + CRC
    if (length < expected_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    // Vérifier CRC
    uint16_t crc_expected = static_cast<uint16_t>(frame[expected_len - 2]) |
                             static_cast<uint16_t>(frame[expected_len - 1] << 8);
    uint16_t crc_computed = uart_frame_builder_crc16(frame, expected_len - 2);
    if (crc_expected != crc_computed) {
        return ESP_ERR_INVALID_CRC;
    }

    *register_count = payload_len / 2;
    return ESP_OK;
}
```

---

## Correction 2 : Support MODBUS 0x10 (Write Multiple Registers)

### Spécifications

**Section** : 1.1.7 (p.6)

**Format de requête** :
```
Byte 1 | Byte 2 | Byte 3    | Byte 4    | Byte 5 | Byte 6 | Byte 7 | Byte 8     | Byte 9     | ...
0xAA   | 0x10   | ADDR:MSB  | ADDR:LSB  | 0x00   | RL     | PL     | DATA1:MSB  | DATA1:LSB  | ...
```

**Format de réponse** :
```
Byte 1 | Byte 2 | Byte 3    | Byte 4    | Byte 5 | Byte 6 | Byte 7  | Byte 8
0xAA   | 0x10   | ADDR:MSB  | ADDR:LSB  | 0x00   | RL     | CRC:LSB | CRC:MSB
```

### Implémentation

#### Fichier : `main/uart_bms/uart_frame_builder.h`

```cpp
/**
 * @brief Build a MODBUS Write Multiple Registers request (0x10)
 *
 * @param buffer Destination buffer
 * @param buffer_size Buffer capacity
 * @param start_address Starting register address (MODBUS format: MSB first)
 * @param values Array of register values to write
 * @param register_count Number of registers to write (max 100)
 * @param out_length Resulting frame length
 * @return ESP_OK on success
 */
esp_err_t uart_frame_builder_build_modbus_write(uint8_t *buffer,
                                                size_t buffer_size,
                                                uint16_t start_address,
                                                const uint16_t *values,
                                                uint8_t register_count,
                                                size_t *out_length);
```

#### Fichier : `main/uart_bms/uart_frame_builder.cpp`

```cpp
esp_err_t uart_frame_builder_build_modbus_write(uint8_t *buffer,
                                                size_t buffer_size,
                                                uint16_t start_address,
                                                const uint16_t *values,
                                                uint8_t register_count,
                                                size_t *out_length)
{
    if (buffer == nullptr || values == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    // Limiter à 100 registres selon la doc
    if (register_count == 0 || register_count > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t payload_len = register_count * 2;
    const size_t required = 7 + payload_len + 2;  // header + payload + CRC
    if (buffer_size < required) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t offset = 0;
    buffer[offset++] = 0xAA;  // Preamble
    buffer[offset++] = 0x10;  // MODBUS Write Multiple Registers
    buffer[offset++] = static_cast<uint8_t>((start_address >> 8) & 0xFF);  // ADDR MSB
    buffer[offset++] = static_cast<uint8_t>(start_address & 0xFF);  // ADDR LSB
    buffer[offset++] = 0x00;  // Reserved
    buffer[offset++] = register_count;  // RL
    buffer[offset++] = static_cast<uint8_t>(payload_len);  // PL

    // Écrire les valeurs en MSB first
    for (size_t i = 0; i < register_count; ++i) {
        buffer[offset++] = static_cast<uint8_t>((values[i] >> 8) & 0xFF);  // MSB
        buffer[offset++] = static_cast<uint8_t>(values[i] & 0xFF);  // LSB
    }

    const uint16_t crc = uart_frame_builder_crc16(buffer, offset);
    buffer[offset++] = static_cast<uint8_t>(crc & 0xFF);  // CRC LSB
    buffer[offset++] = static_cast<uint8_t>((crc >> 8) & 0xFF);  // CRC MSB

    if (out_length != nullptr) {
        *out_length = offset;
    }

    return ESP_OK;
}
```

---

## Correction 3 : Gestion du Sleep Mode

### Spécifications

**Section** : 1.3 (p.12)

> "If Tiny BMS device is in sleep mode, the first command must be send twice."

### Implémentation

#### Fichier : `main/uart_bms/uart_bms.cpp`

Ajouter une fonction interne pour l'envoi avec retry :

```cpp
/**
 * @brief Send UART command with automatic retry for sleep mode wake-up
 *
 * Implements the sleep mode handling as specified in TinyBMS documentation:
 * If BMS is in sleep mode, the first command wakes it up but gets no response,
 * so we must send the command twice.
 *
 * @param frame Command frame to send
 * @param frame_length Length of the frame
 * @param response Buffer for response
 * @param response_size Size of response buffer
 * @param response_length Actual response length received
 * @param timeout_ms Timeout for waiting response
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if no response after retry
 */
static esp_err_t uart_bms_send_with_wakeup(const uint8_t* frame,
                                           size_t frame_length,
                                           uint8_t* response,
                                           size_t response_size,
                                           size_t* response_length,
                                           uint32_t timeout_ms)
{
    if (frame == nullptr || response == nullptr || response_length == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    *response_length = 0;

    // Flush any pending data
    uart_flush_input(UART_BMS_UART_PORT);

    // First send (may wake up BMS from sleep)
    int written = uart_write_bytes(UART_BMS_UART_PORT, frame, frame_length);
    if (written != frame_length) {
        ESP_LOGW(kTag, "Failed to write full frame (wrote %d of %zu bytes)", written, frame_length);
        return ESP_ERR_INVALID_STATE;
    }

    // Wait a bit for potential wake-up
    vTaskDelay(pdMS_TO_TICKS(50));

    // Try to receive response
    int len = uart_read_bytes(UART_BMS_UART_PORT,
                             response,
                             response_size,
                             pdMS_TO_TICKS(timeout_ms));

    // If no response, BMS might have been asleep - retry
    if (len <= 0) {
        ESP_LOGD(kTag, "No response on first attempt, retrying (BMS may have been in sleep mode)");

        // Flush again
        uart_flush_input(UART_BMS_UART_PORT);

        // Second send (BMS should be awake now)
        written = uart_write_bytes(UART_BMS_UART_PORT, frame, frame_length);
        if (written != frame_length) {
            ESP_LOGW(kTag, "Failed to write full frame on retry");
            return ESP_ERR_INVALID_STATE;
        }

        // Wait for response again
        len = uart_read_bytes(UART_BMS_UART_PORT,
                             response,
                             response_size,
                             pdMS_TO_TICKS(timeout_ms));
    }

    if (len > 0) {
        *response_length = static_cast<size_t>(len);
        return ESP_OK;
    }

    ESP_LOGW(kTag, "No response after wake-up retry");
    return ESP_ERR_TIMEOUT;
}
```

Modifier les fonctions d'envoi existantes pour utiliser cette nouvelle fonction :

```cpp
// Dans uart_bms_poll_task
static void uart_bms_poll_task(void *arg)
{
    // ...
    while (!s_task_should_exit) {
        // ...

        // Utiliser la nouvelle fonction avec wake-up handling
        esp_err_t send_err = uart_bms_send_with_wakeup(
            s_poll_request,
            s_poll_request_length,
            s_rx_buffer,
            sizeof(s_rx_buffer),
            &rx_len,
            200  // 200ms timeout
        );

        if (send_err == ESP_OK && rx_len > 0) {
            // Traiter la réponse...
        }

        // ...
    }
}
```

---

## Correction 4 : Read Newest Events (0x11)

### Spécifications

**Section** : 1.1.9 (p.7)

**Format de requête** :
```
Byte 1 | Byte 2 | Byte 3  | Byte 4
0xAA   | 0x11   | CRC:LSB | CRC:MSB
```

**Format de réponse** (MSG 1 - BMS timestamp) :
```
Byte 1 | Byte 2 | Byte 3 | Byte 4   | Byte 5 | Byte 6 | Byte 7    | Byte 8
0xAA   | 0x11   | PL     | BTSP:LSB | BTSP   | BTSP   | BTSP:MSB  | 0x00
```

**Format de réponse** (MSG n - Event) :
```
Byte 1 | Byte 2 | Byte 3 | Byte 4   | Byte 5 | Byte 6    | Byte 7 | Byte 8
0xAA   | 0x11   | PL     | TSPn:LSB | TSPn   | TSPn:MSB  | IDn    | n-1
```

### Structures de Données

#### Fichier : `main/uart_bms/uart_bms.h`

```cpp
#define UART_BMS_MAX_EVENTS 50

/**
 * @brief TinyBMS Event entry
 */
typedef struct {
    uint32_t timestamp_sec;  ///< Event timestamp in seconds
    uint8_t event_id;        ///< Event ID (see TinyBMS doc Chapter 4)
} uart_bms_event_t;

/**
 * @brief TinyBMS Events data
 */
typedef struct {
    uint32_t bms_timestamp_sec;  ///< Current BMS timestamp
    uart_bms_event_t events[UART_BMS_MAX_EVENTS];
    size_t event_count;
    uint64_t gateway_timestamp_ms;  ///< Gateway timestamp when received
} uart_bms_events_data_t;
```

### Implémentation

#### Fichier : `main/uart_bms/uart_frame_builder.h`

```cpp
/**
 * @brief Build Read Newest Events request (0x11)
 */
esp_err_t uart_frame_builder_build_read_events(uint8_t *buffer,
                                               size_t buffer_size,
                                               size_t *out_length);
```

#### Fichier : `main/uart_bms/uart_frame_builder.cpp`

```cpp
esp_err_t uart_frame_builder_build_read_events(uint8_t *buffer,
                                               size_t buffer_size,
                                               size_t *out_length)
{
    if (buffer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    constexpr size_t kEventsRequestSize = 4;  // AA 11 CRC:LSB CRC:MSB
    if (buffer_size < kEventsRequestSize) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t offset = 0;
    buffer[offset++] = 0xAA;  // Preamble
    buffer[offset++] = 0x11;  // Read Newest Events

    const uint16_t crc = uart_frame_builder_crc16(buffer, offset);
    buffer[offset++] = static_cast<uint8_t>(crc & 0xFF);
    buffer[offset++] = static_cast<uint8_t>((crc >> 8) & 0xFF);

    if (out_length != nullptr) {
        *out_length = offset;
    }

    return ESP_OK;
}
```

#### Fichier : `main/uart_bms/uart_response_parser.h`

```cpp
class UartResponseParser {
public:
    // Existing methods...

    /**
     * @brief Parse Read Events response frames (multi-frame response)
     *
     * @param frames Array of response frames
     * @param frame_count Number of frames received
     * @param frame_lengths Length of each frame
     * @param timestamp_ms Gateway timestamp
     * @param events_out Output events structure
     * @return ESP_OK on success
     */
    esp_err_t parseEventsFrames(const uint8_t** frames,
                                size_t frame_count,
                                const size_t* frame_lengths,
                                uint64_t timestamp_ms,
                                uart_bms_events_data_t* events_out);
};
```

#### Fichier : `main/uart_bms/uart_response_parser.cpp`

```cpp
esp_err_t UartResponseParser::parseEventsFrames(const uint8_t** frames,
                                                size_t frame_count,
                                                const size_t* frame_lengths,
                                                uint64_t timestamp_ms,
                                                uart_bms_events_data_t* events_out)
{
    if (frames == nullptr || frame_lengths == nullptr || events_out == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    if (frame_count == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    std::memset(events_out, 0, sizeof(*events_out));
    events_out->gateway_timestamp_ms = timestamp_ms;

    // Premier frame contient le BMS timestamp
    const uint8_t* first_frame = frames[0];
    size_t first_len = frame_lengths[0];

    if (first_len < 10) {  // AA 11 PL BTSP(4) 0x00 CRC(2)
        return ESP_ERR_INVALID_SIZE;
    }

    if (first_frame[0] != 0xAA || first_frame[1] != 0x11) {
        return ESP_ERR_INVALID_STATE;
    }

    // Extraire BMS timestamp (UINT32 LSB first)
    events_out->bms_timestamp_sec =
        static_cast<uint32_t>(first_frame[3]) |
        (static_cast<uint32_t>(first_frame[4]) << 8) |
        (static_cast<uint32_t>(first_frame[5]) << 16) |
        (static_cast<uint32_t>(first_frame[6]) << 24);

    // Parser les frames d'events suivants
    size_t event_index = 0;
    for (size_t i = 1; i < frame_count && event_index < UART_BMS_MAX_EVENTS; ++i) {
        const uint8_t* frame = frames[i];
        size_t len = frame_lengths[i];

        if (len < 10) {  // AA 11 PL TSP(3) ID PKT CRC(2)
            continue;
        }

        if (frame[0] != 0xAA || frame[1] != 0x11) {
            continue;
        }

        // Extraire timestamp (UINT24 LSB first)
        uint32_t event_timestamp =
            static_cast<uint32_t>(frame[3]) |
            (static_cast<uint32_t>(frame[4]) << 8) |
            (static_cast<uint32_t>(frame[5]) << 16);

        // Extraire event ID
        uint8_t event_id = frame[6];

        events_out->events[event_index].timestamp_sec = event_timestamp;
        events_out->events[event_index].event_id = event_id;
        event_index++;
    }

    events_out->event_count = event_index;
    return ESP_OK;
}
```

---

## Tests Unitaires

### Fichier : `test/test_uart_modbus.c`

```c
#include "unity.h"
#include "uart_frame_builder.h"
#include "uart_response_parser.h"

void test_modbus_read_frame_format(void)
{
    uint8_t buffer[16];
    size_t length;

    esp_err_t err = uart_frame_builder_build_modbus_read(buffer, sizeof(buffer),
                                                         0x0024, 2, &length);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(8, length);
    TEST_ASSERT_EQUAL_HEX8(0xAA, buffer[0]);  // Preamble
    TEST_ASSERT_EQUAL_HEX8(0x03, buffer[1]);  // Opcode
    TEST_ASSERT_EQUAL_HEX8(0x00, buffer[2]);  // ADDR MSB
    TEST_ASSERT_EQUAL_HEX8(0x24, buffer[3]);  // ADDR LSB
    TEST_ASSERT_EQUAL_HEX8(0x00, buffer[4]);  // Reserved
    TEST_ASSERT_EQUAL_HEX8(0x02, buffer[5]);  // Register count

    // Verify CRC
    uint16_t crc_calculated = uart_frame_builder_crc16(buffer, 6);
    uint16_t crc_in_frame = buffer[6] | (buffer[7] << 8);
    TEST_ASSERT_EQUAL_UINT16(crc_calculated, crc_in_frame);
}

void test_modbus_write_frame_format(void)
{
    uint8_t buffer[32];
    size_t length;
    uint16_t values[] = {0x1234, 0x5678};

    esp_err_t err = uart_frame_builder_build_modbus_write(buffer, sizeof(buffer),
                                                          0x013B, values, 2, &length);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(13, length);  // 7 header + 4 data + 2 CRC
    TEST_ASSERT_EQUAL_HEX8(0xAA, buffer[0]);  // Preamble
    TEST_ASSERT_EQUAL_HEX8(0x10, buffer[1]);  // Opcode
    TEST_ASSERT_EQUAL_HEX8(0x01, buffer[2]);  // ADDR MSB
    TEST_ASSERT_EQUAL_HEX8(0x3B, buffer[3]);  // ADDR LSB
    TEST_ASSERT_EQUAL_HEX8(0x00, buffer[4]);  // Reserved
    TEST_ASSERT_EQUAL_HEX8(0x02, buffer[5]);  // Register count
    TEST_ASSERT_EQUAL_HEX8(0x04, buffer[6]);  // Payload length

    // Verify data (MSB first!)
    TEST_ASSERT_EQUAL_HEX8(0x12, buffer[7]);   // Value1 MSB
    TEST_ASSERT_EQUAL_HEX8(0x34, buffer[8]);   // Value1 LSB
    TEST_ASSERT_EQUAL_HEX8(0x56, buffer[9]);   // Value2 MSB
    TEST_ASSERT_EQUAL_HEX8(0x78, buffer[10]);  // Value2 LSB
}

void test_sleep_mode_retry_logic(void)
{
    // This would be an integration test with actual hardware
    // or a mock UART driver
    TEST_IGNORE_MESSAGE("Requires hardware or mock");
}

void test_read_events_parsing(void)
{
    // Test data: BMS timestamp frame
    uint8_t frame1[] = {0xAA, 0x11, 0x05, 0x10, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00};
    // Calculate and add CRC...

    // Test data: Event frame
    uint8_t frame2[] = {0xAA, 0x11, 0x04, 0x10, 0x27, 0x00, 0x02, 0x00, 0x00, 0x00};
    // Calculate and add CRC...

    // Parse and verify...
    TEST_IGNORE_MESSAGE("Implementation needed");
}
```

---

## Validation

### Checklist de Conformité

Après implémentation, valider :

- [ ] MODBUS 0x03 : Format de requête conforme (MSB first)
- [ ] MODBUS 0x03 : Parsing de réponse correct (MSB first)
- [ ] MODBUS 0x10 : Format de requête conforme (MSB first, max 100 reg)
- [ ] MODBUS 0x10 : Parsing de réponse ACK correct
- [ ] Sleep mode : Double-send implémenté
- [ ] Sleep mode : Retry automatique si pas de réponse
- [ ] Read Events 0x11 : Format de requête correct
- [ ] Read Events 0x11 : Parsing multi-frame correct
- [ ] Tests unitaires passent tous
- [ ] Test avec hardware réel réussi
- [ ] Documentation à jour

### Tests d'Intégration

1. **Test MODBUS Read avec BMS réel** :
   ```
   - Envoyer commande 0x03 pour lire registres 0x0024-0x0027
   - Vérifier format MSB first dans la réponse
   - Comparer avec lecture via 0x09 (doit être identique)
   ```

2. **Test MODBUS Write avec BMS réel** :
   ```
   - Écrire registre de config via 0x10
   - Vérifier ACK
   - Relire via 0x03 pour confirmer
   ```

3. **Test Sleep Mode** :
   ```
   - Laisser BMS en idle pendant 5 minutes
   - Envoyer commande
   - Vérifier que le retry automatique fonctionne
   ```

4. **Test Read Events** :
   ```
   - Déclencher un event (ex: low voltage warning)
   - Lire avec 0x11
   - Vérifier parsing correct de tous les frames
   ```

---

## Commit Strategy

### Commit 1 : MODBUS Read Support
```
feat(uart): Add MODBUS 0x03 Read Holding Registers support

- Implement uart_frame_builder_build_modbus_read()
- Add MODBUS response parser (MSB first format)
- Add unit tests for MODBUS read frames
- Update documentation

Refs: TinyBMS Communication Protocols Rev D, Section 1.1.6
```

### Commit 2 : MODBUS Write Support
```
feat(uart): Add MODBUS 0x10 Write Multiple Registers support

- Implement uart_frame_builder_build_modbus_write()
- Support up to 100 registers as per spec
- MSB first format for MODBUS compatibility
- Add unit tests

Refs: TinyBMS Communication Protocols Rev D, Section 1.1.7
```

### Commit 3 : Sleep Mode Handling
```
fix(uart): Implement sleep mode wake-up handling

- Add automatic retry on first command timeout
- Implement double-send logic as per TinyBMS spec
- Improves reliability after idle periods

Refs: TinyBMS Communication Protocols Rev D, Section 1.3
```

### Commit 4 : Events Reading
```
feat(uart): Add Read Newest Events (0x11) support

- Implement multi-frame events parsing
- Add uart_bms_events_data_t structure
- Support up to 50 events per read
- Add event timestamp handling

Refs: TinyBMS Communication Protocols Rev D, Section 1.1.9
```

---

## Timeline

- **Commit 1** : 2 heures (MODBUS 0x03)
- **Commit 2** : 1.5 heures (MODBUS 0x10)
- **Commit 3** : 1 heure (Sleep mode)
- **Commit 4** : 2 heures (Events)
- **Tests & Validation** : 2 heures

**Total** : ~8.5 heures de développement

---

## Notes Importantes

1. **Ordre des Bytes** :
   - MODBUS (0x03, 0x10) : **MSB first** (Big Endian)
   - Propriétaires (0x07, 0x09, 0x0D) : **LSB first** (Little Endian)

2. **Limites** :
   - MODBUS Read : Max 127 registres (single packet)
   - MODBUS Write : Max 100 registres (selon doc)
   - Events : Max 50 events par lecture (limitation gateway)

3. **Compatibilité** :
   - Ces ajouts sont rétro-compatibles
   - Les commandes existantes continuent de fonctionner
   - Pas de breaking changes

4. **Performance** :
   - Sleep mode retry ajoute ~50ms de latence initiale
   - Acceptable pour un système de monitoring

---

**Fin du Plan d'Implémentation**
