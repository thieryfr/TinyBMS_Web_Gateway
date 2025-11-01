# TinyBMS MQTT Battery Exposure — Draft

## 1. Modbus battery registers to expose

The following registers from Victron's CCGX Modbus-TCP catalogue cover the core TinyBMS telemetry we plan to expose. Scaling factors follow the official list (positive factors indicate multiplication, negative factors division).

| Address | Description | Type | Scale | Unit | dbus path | Remarks |
|---------|-------------|------|-------|------|-----------|---------|
| 256 | Battery power | int32 | 1 | W | /Dc/0/Power | — |
| 259 | Battery voltage | uint16 | 100 | V DC | /Dc/0/Voltage | — |
| 261 | Current | int16 | 10 | A DC | /Dc/0/Current | Positive = charging, negative = discharging |
| 262 | Battery temperature | int16 | 10 | °C | /Dc/0/Temperature | — |
| 263 | Mid-point voltage of the battery bank | uint16 | 100 | V DC | /Dc/0/MidVoltage | Requires TinyBMS mid-point support |
| 264 | Mid-point deviation of the battery bank | uint16 | 100 | % | /Dc/0/MidVoltageDeviation | Requires TinyBMS mid-point support |
| 265 | Consumed Amp-hours | uint16 | -10 | Ah | /ConsumedAmphours | Always negative in Victron spec |
| 266 | State of charge | uint16 | 10 | % | /Soc | — |
| 304 | State of health | uint16 | 10 | % | /Soh | Marked “CAN.Bus batteries only” in Victron list |
| 307 | Max charge current | uint16 | 10 | A DC | /Info/MaxChargeCurrent | CAN BMS only |
| 308 | Max discharge current | uint16 | 10 | A DC | /Info/MaxDischargeCurrent | CAN BMS only |
| 320 | High charge current alarm | uint16 | 1 | level | /Alarms/HighChargeCurrent | 0 = normal, 2 = alarm |
| 321 | High discharge current alarm | uint16 | 1 | level | /Alarms/HighDischargeCurrent | 0 = normal, 2 = alarm |
| 322 | Cell imbalance alarm | uint16 | 1 | level | /Alarms/CellImbalance | 0 = normal, 2 = alarm |
| 1285 | Balancing activity | uint16 | 1 | flag | /Balancing | Bitmask of active cell balancing |
| 1290 | System minimum cell voltage | uint16 | 100 | V DC | /System/MinCellVoltage | — |
| 1291 | System maximum cell voltage | uint16 | 100 | V DC | /System/MaxCellVoltage | — |

## 2. Mapping to existing event bus data

| Register(s) | Event bus source | Availability | Notes |
|-------------|------------------|--------------|-------|
| 256 (power) | `pack_voltage_v` × `pack_current_a` from `uart_bms_live_data_t` | Derived | Computed when publishing MQTT snapshot to avoid storing separate field. |
| 259 (voltage) | `pack_voltage_v` | ✅ | Published in TinyBMS live data frames. |
| 261 (current) | `pack_current_a` | ✅ | Sign follows Victron convention. |
| 262 (temperature) | `average_temperature_c` | ✅ | Average pack temperature already in telemetry. |
| 263–264 (mid-point metrics) | — | ⚠️ Missing | No mid-point voltage in current live data; requires TinyBMS register parsing extension. |
| 265 (consumed Ah) | — | ⚠️ Missing | Energy integrator not implemented; would require cumulative tracking similar to CAN energy counters. |
| 266 (SoC) | `state_of_charge_pct` | ✅ | Published as percentage; rescale to register units (×10). |
| 304 (SoH) | `state_of_health_pct` | ✅ | Provided by decoder; rescale to register units. |
| 307 (max charge current) | `max_charge_current` | ✅ | Parser fills 0.1 A scaled field from TinyBMS register. |
| 308 (max discharge current) | `max_discharge_current` | ✅ | Stored in 0.1 A units. |
| 320–322 (alarms) | `alarm_bits` / `warning_bits` bitfields | ⚠️ Partial | Need bit-to-alarm mapping to expose individual Victron alarm registers. |
| 1285 (balancing) | `balancing_bits` | ✅ | Bitmask already published; convert to Victron flag semantics. |
| 1290–1291 (min/max cell voltage) | `min_cell_mv` / `max_cell_mv` | ✅ | Convert from millivolts to register scaling (÷100). |

`uart_bms_live_data_t` carries all TinyBMS live telemetry on the event bus, consumed by `monitoring` for JSON snapshots and by the CAN publisher for Victron PGNs.【F:main/uart_bms/uart_bms.h†L22-L70】【F:main/monitoring/monitoring.c†L88-L149】 The monitoring module already emits aggregated telemetry events whenever new UART data arrives, so MQTT can subscribe to the same event stream without additional polling.【F:main/monitoring/monitoring.c†L152-L193】

## 3. Additional metrics and availability check

* **Per-frame timestamps, uptime, cycle count.** Present in `uart_bms_live_data_t` and included in monitoring snapshots; expose in MQTT for historical alignment.【F:main/uart_bms/uart_bms.h†L37-L70】【F:main/monitoring/monitoring.c†L100-L123】
* **Energy counters (Wh in/out).** `can_publisher` already integrates energy for Victron PGN 0x378; reuse its accumulator for MQTT once the helper exposes read access.【F:main/can_publisher/conversion_table.c†L16-L122】
* **Cell-level alarms.** Decoder provides `alarm_bits`/`warning_bits` but lacks mapping to Victron alarm registers; implement helper translating TinyBMS bit positions to Victron semantics before publishing `/Alarms/*` registers.【F:main/uart_bms/uart_bms.h†L47-L49】
* **Mid-point metrics.** TinyBMS register snapshots (in `registers[]`) may already contain the raw words; confirm addresses and extend parser to populate dedicated fields before exposing registers 263–264.【F:main/uart_bms/uart_bms.h†L68-L69】

## 4. Proposed MQTT topics and payload formats

| Topic | Format | Retain | Payload schema |
|-------|--------|--------|----------------|
| `tinybms/status` | JSON | ✅ | `{ "fw_version": "x.y.z", "uptime_s": <uint32>, "online": true, "last_error": "" }` sourced from monitoring snapshot and diagnostics counters. |
| `tinybms/metrics` | CBOR | ❌ | CBOR map mirroring Modbus register set: `{ 0x0100: <power_W>, 0x0103: <voltage_V>, 0x0105: <current_A>, 0x010A: <soc_pct>, … }` to keep frame size low for high-rate publishing. |
| `tinybms/alarms` | JSON | ❌ | `{ "high_charge": <level>, "high_discharge": <level>, "cell_imbalance": <level>, "raw_bits": <uint16> }` once alarm mapping is implemented. |
| `tinybms/registers` | CBOR | ❌ | Snapshot of raw TinyBMS register words `{ "timestamp": <ms>, "registers": [ { "address": <uint16>, "value": <uint16> }, … ] }` using data already serialized by `monitoring`. |
| `tinybms/config` | JSON | ✅ | Used for publishing applied configuration and responding to remote writes; reuses existing topic constant in `mqtt_topics.h`. |

CBOR is chosen for high-frequency numeric payloads (`tinybms/metrics`, `tinybms/registers`) to minimise bandwidth, while JSON remains for human-readable status and alarms. MQTT client hooks already define the topic names, so the implementation only needs to serialise the chosen structure in the `mqtt_client` task.【F:main/mqtt_client/mqtt_client.c†L1-L16】【F:main/mqtt_client/mqtt_topics.h†L1-L5】

## 5. Open points for validation

1. Confirm TinyBMS mid-point and energy registers are available via UART; otherwise drop Modbus registers 263–265 from the initial scope.
2. Document TinyBMS alarm bit layout to derive Victron `/Alarms/*` registers before enabling the MQTT alarm payload.
3. Provide a shared helper exposing `can_publisher` energy accumulators to reuse in MQTT metrics.
4. Decide MQTT publish interval (align with CAN publisher period or use dedicated rate).

