#include "uart_response_parser.h"

#include <cstring>

#include "esp_log.h"

#include "uart_frame_builder.h"

namespace {
constexpr uint8_t kTinyBmsPreamble = 0xAA;
constexpr uint8_t kTinyBmsOpcodeReadIndividual = 0x09;
constexpr size_t kFrameHeaderSize = 3;  // preamble + opcode + payload length
constexpr size_t kCrcSize = 2;

TinyRegisterValueType toTinyValueType(uart_bms_value_type_t value_type)
{
    switch (value_type) {
        case UART_BMS_VALUE_UINT16:
            return TinyRegisterValueType::Uint16;
        case UART_BMS_VALUE_INT16:
            return TinyRegisterValueType::Int16;
        case UART_BMS_VALUE_UINT32:
            return TinyRegisterValueType::Uint32;
        case UART_BMS_VALUE_FLOAT32:
            return TinyRegisterValueType::Float;
        case UART_BMS_VALUE_INT8_PAIR:
            return TinyRegisterValueType::Int16;
        default:
            return TinyRegisterValueType::Unknown;
    }
}

int32_t toSignedRaw(uint16_t value)
{
    return static_cast<int32_t>(static_cast<int16_t>(value));
}

const char* kLogTag = "uart_parser";
}  // namespace

UartResponseParser::UartResponseParser() = default;

esp_err_t UartResponseParser::validateFrame(const uint8_t* frame,
                                            size_t length,
                                            size_t* register_count) const
{
    if (frame == nullptr || register_count == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    if (length < kFrameHeaderSize + kCrcSize) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (frame[0] != kTinyBmsPreamble || frame[1] != kTinyBmsOpcodeReadIndividual) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t payload_len = frame[2];
    if ((payload_len % 2) != 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t expected_len = kFrameHeaderSize + payload_len + kCrcSize;
    if (length < expected_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint16_t crc_expected = static_cast<uint16_t>(frame[expected_len - 2]) |
                             static_cast<uint16_t>(frame[expected_len - 1] << 8);
    uint16_t crc_computed = uart_frame_builder_crc16(frame, expected_len - kCrcSize);
    if (crc_expected != crc_computed) {
        return ESP_ERR_INVALID_CRC;
    }

    size_t words = payload_len / 2;
    if (words == 0 || words > UART_BMS_MAX_REGISTERS) {
        return ESP_ERR_INVALID_SIZE;
    }

    *register_count = words;
    return ESP_OK;
}

void UartResponseParser::appendSnapshot(TinyBMS_LiveData& shared_out,
                                        uint16_t address,
                                        uart_bms_value_type_t value_type,
                                        int32_t raw_value,
                                        uint8_t word_count,
                                        const uint16_t* word_ptr)
{
    const TinyRegisterValueType tiny_type = toTinyValueType(value_type);
    if (!shared_out.appendSnapshot(address,
                                   tiny_type,
                                   raw_value,
                                   word_count,
                                   nullptr,
                                   word_ptr)) {
        ESP_LOGW(kLogTag, "Snapshot buffer full while storing register 0x%04X", address);
        diagnostics_.missing_register_errors++;
    }
}

void UartResponseParser::decodeRegisters(const uint8_t* frame,
                                         size_t register_count,
                                         uart_bms_live_data_t* legacy_out,
                                         TinyBMS_LiveData* shared_out)
{
    uint16_t raw_words[UART_BMS_MAX_REGISTERS] = {0};
    for (size_t i = 0; i < register_count; ++i) {
        uint16_t raw = static_cast<uint16_t>(frame[3 + i * 2]) |
                       static_cast<uint16_t>(frame[4 + i * 2] << 8);
        raw_words[i] = raw;

        if (legacy_out != nullptr) {
            uart_bms_register_entry_t entry{};
            if (i < UART_BMS_REGISTER_WORD_COUNT) {
                entry.address = g_uart_bms_poll_addresses[i];
            }
            entry.raw_value = raw;
            legacy_out->registers[i] = entry;
        }
    }

    if (legacy_out != nullptr) {
        legacy_out->register_count = register_count;
    }

    if (shared_out != nullptr) {
        shared_out->resetSnapshots();
    }

    size_t word_index = 0;
    for (size_t meta_index = 0; meta_index < g_uart_bms_register_count; ++meta_index) {
        const uart_bms_register_metadata_t& meta = g_uart_bms_registers[meta_index];
        if (word_index + meta.word_count > register_count) {
            ESP_LOGW(kLogTag,
                     "Missing %u word(s) for register 0x%04X",
                     static_cast<unsigned>(meta.word_count),
                     meta.address);
            diagnostics_.missing_register_errors++;
            break;
        }

        const uint16_t* words = &raw_words[word_index];

        switch (meta.type) {
            case UART_BMS_VALUE_UINT16: {
                const uint16_t raw = words[0];
                const float scaled = static_cast<float>(raw) * meta.scale;

                if (legacy_out != nullptr) {
                    switch (meta.primary_field) {
                        case UART_BMS_FIELD_MIN_CELL_MV:
                            legacy_out->min_cell_mv = raw;
                            break;
                        case UART_BMS_FIELD_MAX_CELL_MV:
                            legacy_out->max_cell_mv = raw;
                            break;
                        case UART_BMS_FIELD_STATE_OF_HEALTH:
                            legacy_out->state_of_health_pct = scaled;
                            break;
                        case UART_BMS_FIELD_SYSTEM_STATUS:
                            legacy_out->alarm_bits = raw;
                            break;
                        case UART_BMS_FIELD_NEED_BALANCING:
                            legacy_out->warning_bits = raw;
                            break;
                        case UART_BMS_FIELD_BALANCING_BITS:
                            legacy_out->balancing_bits = raw;
                            break;
                        case UART_BMS_FIELD_MAX_DISCHARGE_CURRENT:
                            legacy_out->max_discharge_current_limit_a = scaled;
                            break;
                        case UART_BMS_FIELD_MAX_CHARGE_CURRENT:
                            legacy_out->max_charge_current_limit_a = scaled;
                            break;
                        case UART_BMS_FIELD_PEAK_DISCHARGE_CURRENT_LIMIT:
                            legacy_out->peak_discharge_current_limit_a = scaled;
                            break;
                        case UART_BMS_FIELD_BATTERY_CAPACITY:
                            legacy_out->battery_capacity_ah = scaled;
                            break;
                        case UART_BMS_FIELD_SERIES_CELL_COUNT:
                            legacy_out->series_cell_count = raw;
                            break;
                        case UART_BMS_FIELD_OVERVOLTAGE_CUTOFF:
                            legacy_out->overvoltage_cutoff_mv = raw;
                            break;
                        case UART_BMS_FIELD_UNDERVOLTAGE_CUTOFF:
                            legacy_out->undervoltage_cutoff_mv = raw;
                            break;
                        case UART_BMS_FIELD_DISCHARGE_OVER_CURRENT_LIMIT:
                            legacy_out->discharge_overcurrent_limit_a = scaled;
                            break;
                        case UART_BMS_FIELD_CHARGE_OVER_CURRENT_LIMIT:
                            legacy_out->charge_overcurrent_limit_a = scaled;
                            break;
                        case UART_BMS_FIELD_OVERHEAT_CUTOFF:
                            legacy_out->overheat_cutoff_c = scaled;
                            break;
                        case UART_BMS_FIELD_HARDWARE_VERSION:
                            legacy_out->hardware_version = static_cast<uint8_t>(raw & 0xFF);
                            if (meta.secondary_field == UART_BMS_FIELD_HARDWARE_CHANGES_VERSION) {
                                legacy_out->hardware_changes_version = static_cast<uint8_t>((raw >> 8) & 0xFF);
                            }
                            break;
                        case UART_BMS_FIELD_FIRMWARE_VERSION:
                            legacy_out->firmware_version = static_cast<uint8_t>(raw & 0xFF);
                            if (meta.secondary_field == UART_BMS_FIELD_FIRMWARE_FLAGS) {
                                legacy_out->firmware_flags = static_cast<uint8_t>((raw >> 8) & 0xFF);
                            }
                            break;
                        case UART_BMS_FIELD_INTERNAL_FIRMWARE_VERSION:
                            legacy_out->internal_firmware_version = raw;
                            break;
                        default:
                            break;
                    }
                }

                if (shared_out != nullptr) {
                    switch (meta.primary_field) {
                        case UART_BMS_FIELD_MIN_CELL_MV:
                            shared_out->min_cell_mv = raw;
                            break;
                        case UART_BMS_FIELD_MAX_CELL_MV:
                            shared_out->max_cell_mv = raw;
                            break;
                        case UART_BMS_FIELD_STATE_OF_HEALTH:
                            shared_out->soh_percent = scaled;
                            shared_out->soh_raw = raw;
                            break;
                        case UART_BMS_FIELD_SYSTEM_STATUS:
                            shared_out->online_status = raw;
                            break;
                        case UART_BMS_FIELD_BALANCING_BITS:
                            shared_out->balancing_bits = raw;
                            break;
                        case UART_BMS_FIELD_MAX_DISCHARGE_CURRENT:
                            shared_out->max_discharge_current = raw;
                            break;
                        case UART_BMS_FIELD_MAX_CHARGE_CURRENT:
                            shared_out->max_charge_current = raw;
                            break;
                        case UART_BMS_FIELD_PEAK_DISCHARGE_CURRENT_LIMIT:
                            shared_out->max_discharge_current = static_cast<uint16_t>(scaled * 10.0f);
                            break;
                        case UART_BMS_FIELD_OVERVOLTAGE_CUTOFF:
                            shared_out->cell_overvoltage_mv = raw;
                            break;
                        case UART_BMS_FIELD_UNDERVOLTAGE_CUTOFF:
                            shared_out->cell_undervoltage_mv = raw;
                            break;
                        case UART_BMS_FIELD_DISCHARGE_OVER_CURRENT_LIMIT:
                            shared_out->discharge_overcurrent_a = static_cast<uint16_t>(scaled);
                            break;
                        case UART_BMS_FIELD_CHARGE_OVER_CURRENT_LIMIT:
                            shared_out->charge_overcurrent_a = static_cast<uint16_t>(scaled);
                            break;
                        case UART_BMS_FIELD_OVERHEAT_CUTOFF:
                            shared_out->overheat_cutoff_c = static_cast<uint16_t>(scaled);
                            break;
                        default:
                            break;
                    }
                }

                if (shared_out != nullptr) {
                    appendSnapshot(*shared_out,
                                   meta.address,
                                   meta.type,
                                   static_cast<int32_t>(raw),
                                   meta.word_count,
                                   words);
                }

                word_index += 1;
                break;
            }
            case UART_BMS_VALUE_INT16: {
                const int16_t raw = static_cast<int16_t>(words[0]);
                const float scaled = static_cast<float>(raw) * meta.scale;

                if (legacy_out != nullptr) {
                    switch (meta.primary_field) {
                        case UART_BMS_FIELD_AVERAGE_TEMPERATURE:
                            legacy_out->average_temperature_c = scaled;
                            break;
                        case UART_BMS_FIELD_AUXILIARY_TEMPERATURE:
                            legacy_out->auxiliary_temperature_c = scaled;
                            break;
                        case UART_BMS_FIELD_MOS_TEMPERATURE:
                            legacy_out->mosfet_temperature_c = scaled;
                            break;
                        case UART_BMS_FIELD_OVERHEAT_CUTOFF:
                            legacy_out->overheat_cutoff_c = scaled;
                            break;
                        case UART_BMS_FIELD_LOW_TEMP_CHARGE_CUTOFF:
                            legacy_out->low_temp_charge_cutoff_c = scaled;
                            break;
                        default:
                            break;
                    }
                }

                if (shared_out != nullptr) {
                    switch (meta.primary_field) {
                        case UART_BMS_FIELD_AVERAGE_TEMPERATURE:
                            shared_out->temperature = raw;
                            break;
                        case UART_BMS_FIELD_OVERHEAT_CUTOFF:
                            shared_out->overheat_cutoff_c = static_cast<uint16_t>(scaled);
                            break;
                        default:
                            break;
                    }
                }

                if (shared_out != nullptr) {
                    appendSnapshot(*shared_out,
                                   meta.address,
                                   meta.type,
                                   static_cast<int32_t>(raw),
                                   meta.word_count,
                                   words);
                }

                word_index += 1;
                break;
            }
            case UART_BMS_VALUE_UINT32: {
                const uint32_t raw = static_cast<uint32_t>(words[0]) |
                                      (static_cast<uint32_t>(words[1]) << 16);
                const float scaled = static_cast<float>(raw) * meta.scale;

                if (legacy_out != nullptr) {
                    switch (meta.primary_field) {
                        case UART_BMS_FIELD_STATE_OF_CHARGE:
                            legacy_out->state_of_charge_pct = scaled;
                            break;
                        case UART_BMS_FIELD_UPTIME_SECONDS:
                            legacy_out->uptime_seconds = raw;
                            break;
                        default:
                            break;
                    }
                }

                if (shared_out != nullptr) {
                    switch (meta.primary_field) {
                        case UART_BMS_FIELD_STATE_OF_CHARGE:
                            shared_out->soc_percent = scaled;
                            shared_out->soc_raw = static_cast<uint16_t>(raw & 0xFFFFu);
                            break;
                        default:
                            break;
                    }
                }

                if (shared_out != nullptr) {
                    appendSnapshot(*shared_out,
                                   meta.address,
                                   meta.type,
                                   static_cast<int32_t>(raw),
                                   meta.word_count,
                                   words);
                }

                word_index += meta.word_count;
                break;
            }
            case UART_BMS_VALUE_FLOAT32: {
                const uint32_t raw = static_cast<uint32_t>(words[0]) |
                                      (static_cast<uint32_t>(words[1]) << 16);
                float value;
                std::memcpy(&value, &raw, sizeof(value));
                value *= meta.scale;

                if (legacy_out != nullptr) {
                    switch (meta.primary_field) {
                        case UART_BMS_FIELD_PACK_VOLTAGE:
                            legacy_out->pack_voltage_v = value;
                            break;
                        case UART_BMS_FIELD_PACK_CURRENT:
                            legacy_out->pack_current_a = value;
                            break;
                        default:
                            break;
                    }
                }

                if (shared_out != nullptr) {
                    switch (meta.primary_field) {
                        case UART_BMS_FIELD_PACK_VOLTAGE:
                            shared_out->voltage = value;
                            break;
                        case UART_BMS_FIELD_PACK_CURRENT:
                            shared_out->current = value;
                            break;
                        default:
                            break;
                    }
                }

                if (shared_out != nullptr) {
                    appendSnapshot(*shared_out,
                                   meta.address,
                                   meta.type,
                                   static_cast<int32_t>(raw),
                                   meta.word_count,
                                   words);
                }

                word_index += meta.word_count;
                break;
            }
            case UART_BMS_VALUE_INT8_PAIR: {
                const uint16_t raw = words[0];
                const int8_t low = static_cast<int8_t>(raw & 0xFF);
                const int8_t high = static_cast<int8_t>((raw >> 8) & 0xFF);
                const float low_scaled = static_cast<float>(low) * meta.scale;
                const float high_scaled = static_cast<float>(high) * meta.scale;

                if (legacy_out != nullptr) {
                    if (meta.primary_field == UART_BMS_FIELD_PACK_TEMPERATURE_MIN) {
                        legacy_out->pack_temperature_min_c = low_scaled;
                    }
                    if (meta.secondary_field == UART_BMS_FIELD_PACK_TEMPERATURE_MAX) {
                        legacy_out->pack_temperature_max_c = high_scaled;
                    }
                }

                if (shared_out != nullptr) {
                    if (meta.primary_field == UART_BMS_FIELD_PACK_TEMPERATURE_MIN) {
                        shared_out->pack_temp_min = static_cast<int16_t>(low * 10);
                    }
                    if (meta.secondary_field == UART_BMS_FIELD_PACK_TEMPERATURE_MAX) {
                        shared_out->pack_temp_max = static_cast<int16_t>(high * 10);
                    }
                }

                if (shared_out != nullptr) {
                    appendSnapshot(*shared_out,
                                   meta.address,
                                   meta.type,
                                   toSignedRaw(raw),
                                   meta.word_count,
                                   words);
                }

                word_index += 1;
                break;
            }
            default:
                word_index += meta.word_count;
                break;
        }
    }

    if (shared_out != nullptr) {
        shared_out->cell_imbalance_mv = (shared_out->max_cell_mv > shared_out->min_cell_mv)
                                            ? static_cast<uint16_t>(shared_out->max_cell_mv - shared_out->min_cell_mv)
                                            : 0;
    }
}

esp_err_t UartResponseParser::parseFrame(const uint8_t* frame,
                                         size_t length,
                                         uint64_t timestamp_ms,
                                         uart_bms_live_data_t* legacy_out,
                                         TinyBMS_LiveData* shared_out)
{
    diagnostics_.frames_total++;

    size_t register_count = 0;
    esp_err_t validation = validateFrame(frame, length, &register_count);
    if (validation != ESP_OK) {
        switch (validation) {
            case ESP_ERR_INVALID_CRC:
                diagnostics_.crc_errors++;
                ESP_LOGW(kLogTag, "CRC mismatch on TinyBMS frame");
                break;
            case ESP_ERR_INVALID_STATE:
                diagnostics_.header_errors++;
                ESP_LOGW(kLogTag, "Unexpected TinyBMS frame header");
                break;
            default:
                diagnostics_.length_errors++;
                ESP_LOGW(kLogTag, "Invalid TinyBMS frame length (%zu)", length);
                break;
        }
        return validation;
    }

    if (legacy_out != nullptr) {
        std::memset(legacy_out, 0, sizeof(*legacy_out));
        legacy_out->timestamp_ms = timestamp_ms;
    }

    if (shared_out != nullptr) {
        *shared_out = TinyBMS_LiveData{};
    }

    decodeRegisters(frame, register_count, legacy_out, shared_out);

    diagnostics_.frames_valid++;
    return ESP_OK;
}

void UartResponseParser::recordTimeout()
{
    diagnostics_.timeout_errors++;
    ESP_LOGW(kLogTag, "TinyBMS poll timeout detected");
}

void UartResponseParser::getDiagnostics(uart_bms_parser_diagnostics_t* out) const
{
    if (out == nullptr) {
        return;
    }
    *out = diagnostics_;
}

