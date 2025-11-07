# Conversion Table Module Documentation

## Overview

The `conversion_table` module is responsible for converting TinyBMS UART data to Victron CAN bus format. This is a core component of the TinyBMS-GW gateway that bridges TinyBMS battery management data with Victron Energy systems.

**File**: `main/can_publisher/conversion_table.c` (1436 lines)
**Purpose**: Data transformation, energy tracking, and Victron PGN encoding

---

## Module Architecture

### Logical Sections

The module is organized into the following logical sections:

#### 1. **Energy Management** (Lines ~55-240)
- Energy counter state variables
- Persistent storage integration
- Energy accumulation and integration
- NVS (Non-Volatile Storage) persistence

**Key Functions**:
- `ensure_energy_storage_ready()` - Initialize NVS storage
- `set_energy_state_internal()` - Update energy counters (mutex-protected)
- `update_energy_counters()` - Integrate power to energy (mutex-protected)
- `persist_energy_state_internal()` - Save to NVS
- `maybe_persist_energy()` - Conditional persistence logic

**Thread Safety**: Protected by `s_energy_mutex`

#### 2. **Utility Functions** (Lines ~300-500)
- Data encoding helpers
- Type conversions
- ASCII string handling
- Scaling and range operations

**Key Functions**:
- `encode_u16_scaled()` - Scale float to uint16
- `encode_i16_scaled()` - Scale float to int16
- `encode_2bit_field()` - Bit field manipulation
- `encode_energy_wh()` - Convert Wh to CAN format
- `decode_ascii_from_registers()` - Extract ASCII strings
- `encode_identifier_string()` - Format text for CAN

#### 3. **PGN Encoders** (Lines ~500-1200)
Victron CAN PGN (Parameter Group Number) encoding functions. Each function encodes specific battery data into CAN frames following Victron's protocol.

**Encoder Functions** (ordered by PGN):
- `encode_battery_identification()` - 0x307 Handshake
- `encode_inverter_identifier()` - 0x307 variant
- `encode_charge_limits()` - 0x351 CVL/CCL/DCL
- `encode_soc_soh()` - 0x355 SOC/SOH
- `encode_voltage_current_temperature()` - 0x356 V/I/T
- `encode_alarm_status()` - 0x35A Alarms/Warnings
- `encode_manufacturer_info()` - 0x35E Manufacturer string
- `encode_battery_info()` - 0x35F Battery specs
- `encode_battery_name_part1()` - 0x370 Name part 1
- `encode_battery_name_part2()` - 0x371 Name part 2
- `encode_module_status_counts()` - 0x372 Module counts
- `encode_cell_voltage_temperature_extremes()` - 0x373 Min/Max values
- `encode_min_cell_identifier()` - 0x374 Min cell ID
- `encode_max_cell_identifier()` - 0x375 Max cell ID
- `encode_min_temp_identifier()` - 0x376 Min temp sensor ID
- `encode_max_temp_identifier()` - 0x377 Max temp sensor ID
- `encode_energy_counters()` - 0x378 Energy In/Out (mutex-protected)
- `encode_installed_capacity()` - 0x379 Ah capacity
- `encode_serial_number_part1()` - 0x380 Serial part 1
- `encode_serial_number_part2()` - 0x381 Serial part 2
- `encode_battery_family()` - 0x382 Family string

#### 4. **Data Resolution** (Lines ~600-700)
Functions to resolve battery metadata from UART registers or configuration.

**Key Functions**:
- `resolve_manufacturer_string()` - Get manufacturer name
- `resolve_battery_name_string()` - Get battery name
- `resolve_serial_number_string()` - Get serial number

#### 5. **Channel Registry** (Lines ~1300-1436)
Registry of all CAN channels with their PGN, encoder functions, and publishing intervals.

**Key Components**:
- `g_can_publisher_channels[]` - Array of channel descriptors
- `g_can_publisher_channel_count` - Total number of channels

---

## Thread Safety

### Protected Resources

The module uses **mutex protection** for energy counters to prevent race conditions:

**Mutex**: `s_energy_mutex`

**Protected Variables**:
- `s_energy_charged_wh` - Total charged energy (Wh)
- `s_energy_discharged_wh` - Total discharged energy (Wh)
- `s_energy_last_persist_charged_wh` - Last persisted charged value
- `s_energy_last_persist_discharged_wh` - Last persisted discharged value
- `s_energy_last_timestamp_ms` - Last update timestamp
- `s_energy_dirty` - Persistence flag

**Protected Functions**:
- `set_energy_state_internal()` - Atomic state updates
- `update_energy_counters()` - Atomic integration calculations
- `encode_energy_counters()` - Atomic counter reads for CAN frames

### Thread Safety Pattern

```c
// Acquire mutex with timeout
if (xSemaphoreTake(s_energy_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    // Perform atomic operations
    s_energy_charged_wh += delta;
    // Release mutex
    xSemaphoreGive(s_energy_mutex);
} else {
    ESP_LOGW(TAG, "Failed to acquire energy mutex");
}
```

---

## Data Flow

```
TinyBMS UART Data
       ↓
update_energy_counters() [mutex-protected]
       ↓
PGN Encoder Functions
       ↓
CAN Publisher
       ↓
Victron CAN Bus
```

### Energy Integration Flow

```
BMS Sample (V, I, timestamp)
       ↓
Calculate Δt and Power (W)
       ↓
Integrate: ΔE = P × Δt [mutex-protected]
       ↓
Accumulate to charged/discharged [mutex-protected]
       ↓
Periodically persist to NVS
```

---

## Configuration

### CAN Settings

Uses centralized defaults from `can_config_defaults.h`:
- `CONFIG_TINYBMS_CAN_MANUFACTURER`
- `CONFIG_TINYBMS_CAN_BATTERY_NAME`
- `CONFIG_TINYBMS_CAN_BATTERY_FAMILY`
- `CONFIG_TINYBMS_CAN_SERIAL_NUMBER`

### Energy Persistence

- **Minimum delta**: 10 Wh (before triggering save)
- **Persistence interval**: 60 seconds (minimum between saves)
- **Storage backend**: NVS (Non-Volatile Storage)

---

## Future Refactoring Plan

### Proposed Module Split

To improve maintainability, the module could be split into:

#### 1. `conversion_table_energy.c/.h` (~250 lines)
- Energy counter state and management
- NVS persistence logic
- Energy integration calculations
- Mutex: `s_energy_mutex`

#### 2. `conversion_table_pgn.c/.h` (~700 lines)
- All PGN encoder functions (0x307 - 0x382)
- Victron protocol-specific encoding
- No shared state

#### 3. `conversion_table_utils.c/.h` (~200 lines)
- Encoding utilities (scale, convert, format)
- ASCII string handling
- Data type conversions
- Pure functions (no state)

#### 4. `conversion_table_core.c/.h` (~300 lines)
- Channel registry
- Configuration management
- Module initialization
- Public API

### Benefits of Split
- ✅ Smaller, focused files (~200-300 lines each)
- ✅ Easier to test individual components
- ✅ Clearer separation of concerns
- ✅ Reduced compilation time for changes
- ✅ Better code organization

### Prerequisites for Safe Refactoring
1. ✅ Thread safety implemented (completed)
2. ⏳ Comprehensive unit tests (next step)
3. ⏳ Integration tests for CAN output
4. ⏳ Documentation of all APIs

---

## Testing Strategy

### Unit Tests Needed

1. **Energy Integration**
   - Test accumulation over multiple samples
   - Test timestamp gap handling
   - Test charge/discharge separation
   - Test mutex protection

2. **PGN Encoders**
   - Test each encoder with valid data
   - Test boundary conditions
   - Test NULL handling
   - Test frame format compliance

3. **Utilities**
   - Test scaling functions with edge cases
   - Test ASCII encoding/decoding
   - Test range clamping

### Integration Tests

- Verify CAN frame output matches Victron spec
- Test energy persistence across reboots
- Test concurrent access patterns

---

## Known Issues & Limitations

### Current Limitations
- Large file size (1436 lines) makes navigation difficult
- All encoders in single compilation unit
- Limited error propagation from encoders

### Future Improvements
- Split into multiple files per refactoring plan
- Add comprehensive unit tests
- Improve error handling and reporting
- Add runtime validation of CAN frames
- Performance profiling of encoder functions

---

## References

- **Victron CAN Bus Protocol**: [Victron Energy Documentation](https://www.victronenergy.com/)
- **TinyBMS Protocol**: See `uart_bms_protocol.h`
- **CAN Publisher Module**: See `can_publisher.h`
- **NVS Energy Storage**: See `storage/nvs_energy.h`

---

## Maintenance Notes

### Adding New PGN Encoders

To add a new PGN encoder:

1. Create encoder function:
```c
static bool encode_my_pgn(const uart_bms_live_data_t *data,
                          can_publisher_frame_t *frame)
{
    // Encode logic
    return true;
}
```

2. Add to registry:
```c
{
    .pgn = 0xXXX,
    .can_id = VICTRON_EXTENDED_ID(0xXXX),
    .period_ms = 1000,
    .description = "My PGN",
    .fill_fn = encode_my_pgn,
}
```

3. Update documentation

### Modifying Energy Counters

⚠️ **IMPORTANT**: All modifications to energy counter variables MUST be protected by `s_energy_mutex` to prevent race conditions.

Example:
```c
if (xSemaphoreTake(s_energy_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    s_energy_charged_wh += delta;
    xSemaphoreGive(s_energy_mutex);
}
```

---

**Last Updated**: 2025-11-07
**Version**: 1.0
**Status**: Production with documented thread safety
