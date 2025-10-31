/**
 * @file shared_data.h
 * @brief Shared data structures for FreeRTOS tasks + Logging utilities
 */

#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <Arduino.h>
#include <algorithm>

#include "tiny_read_mapping.h"

constexpr size_t TINY_LIVEDATA_MAX_REGISTERS = 32;

constexpr size_t TINY_REGISTER_MAX_WORDS = 8;

struct TinyRegisterSnapshot {
    int32_t raw_value;
    uint16_t address;
    uint8_t raw_word_count;
    uint8_t type;
    bool has_text;
    String text_value;
    uint16_t raw_words[TINY_REGISTER_MAX_WORDS];
};

// ====================================================================================
// STRUCTURE PRINCIPALE
// ====================================================================================

/**
 * @struct TinyBMS_LiveData
 * @brief Structure de données partagées entre tâches UART, CAN, WebSocket
 *
 * ⚠️ Compatibilité ascendante : cette structure est utilisée par de nombreux modules
 * (publieurs MQTT, mappers CAN, API Web). Son agencement mémoire reste inchangé pour
 * garantir la stabilité de l'API historique. Les nouveaux champs TinyBMS doivent être
 * exposés via `register_snapshots` ou des helpers dédiés plutôt qu'en modifiant le
 * layout existant.
 */
struct TinyBMS_LiveData {
    float voltage;               // V
    float current;               // A (negative = discharge)
    uint16_t min_cell_mv;        // mV
    uint16_t max_cell_mv;        // mV
    uint16_t soc_raw;            // Raw SOC (scale 0.002%)
    uint16_t soh_raw;            // Raw SOH (scale 0.002%)
    int16_t temperature;         // 0.1°C
    int16_t pack_temp_min;       // 0.1°C
    int16_t pack_temp_max;       // 0.1°C
    uint16_t online_status;      // 0x91-0x97 = OK, 0x9B = Fault
    uint16_t balancing_bits;     // Bitfield: active cell balancing
    uint16_t max_discharge_current; // 0.1A
    uint16_t max_charge_current;    // 0.1A
    float soc_percent;           // 0–100%
    float soh_percent;           // 0–100%
    uint16_t cell_imbalance_mv;  // Max - Min cell diff (mV)
    uint16_t cell_overvoltage_mv;    // mV
    uint16_t cell_undervoltage_mv;   // mV
    uint16_t discharge_overcurrent_a; // A
    uint16_t charge_overcurrent_a;    // A
    uint16_t overheat_cutoff_c;       // °C
    uint16_t register_count; // Dynamic register snapshots count
    TinyRegisterSnapshot register_snapshots[TINY_LIVEDATA_MAX_REGISTERS];

    /**
     * @brief Retourne une représentation textuelle formatée (pour logs)
     */
    String toString() const {
        String out;
        out.reserve(128);
        out += "[TinyBMS] ";
        out += "U=" + String(voltage, 2) + "V, ";
        out += "I=" + String(current, 1) + "A, ";
        out += "SOC=" + String(soc_percent, 1) + "%, ";
        out += "SOH=" + String(soh_percent, 1) + "%, ";
        out += "T=" + String(temperature / 10.0, 1) + "°C, ";
        out += "ΔV=" + String(cell_imbalance_mv) + "mV";
        return out;
    }

    void resetSnapshots() {
        register_count = 0;
    }

    bool appendSnapshot(uint16_t address,
                        TinyRegisterValueType type,
                        int32_t raw_value,
                        uint8_t raw_words,
                        const String* text_value,
                        const uint16_t* words_buffer) {
        if (register_count >= TINY_LIVEDATA_MAX_REGISTERS) {
            return false;
        }

        TinyRegisterSnapshot& snap = register_snapshots[register_count++];
        snap.address = address;
        snap.type = static_cast<uint8_t>(type);
        snap.raw_value = raw_value;
        snap.raw_word_count = raw_words;
        snap.has_text = (text_value != nullptr && text_value->length() > 0);
        if (snap.has_text) {
            snap.text_value = *text_value;
        } else {
            snap.text_value = String();
        }

        if (words_buffer && raw_words > 0) {
            uint8_t copy_count = std::min<uint8_t>(raw_words, static_cast<uint8_t>(TINY_REGISTER_MAX_WORDS));
            for (uint8_t i = 0; i < copy_count; ++i) {
                snap.raw_words[i] = words_buffer[i];
            }
            for (uint8_t i = copy_count; i < TINY_REGISTER_MAX_WORDS; ++i) {
                snap.raw_words[i] = 0;
            }
        } else {
            for (uint8_t i = 0; i < TINY_REGISTER_MAX_WORDS; ++i) {
                snap.raw_words[i] = 0;
            }
        }
        return true;
    }

    const TinyRegisterSnapshot* findSnapshot(uint16_t address) const {
        for (uint16_t i = 0; i < register_count; ++i) {
            if (register_snapshots[i].address == address) {
                return &register_snapshots[i];
            }
        }
        return nullptr;
    }

    size_t snapshotCount() const {
        return register_count;
    }

    const TinyRegisterSnapshot& snapshotAt(size_t index) const {
        return register_snapshots[index];
    }

    void applyField(TinyLiveDataField field, float scaled_value, int32_t raw_value) {
        switch (field) {
            case TinyLiveDataField::Voltage:
                voltage = scaled_value;
                break;
            case TinyLiveDataField::Current:
                current = scaled_value;
                break;
            case TinyLiveDataField::SocPercent:
                soc_percent = scaled_value;
                soc_raw = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::SohPercent:
                soh_percent = scaled_value;
                soh_raw = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::Temperature:
                temperature = static_cast<int16_t>(raw_value);
                break;
            case TinyLiveDataField::PackMinTemperature:
                pack_temp_min = static_cast<int16_t>(raw_value * 10);
                break;
            case TinyLiveDataField::PackMaxTemperature:
                pack_temp_max = static_cast<int16_t>(raw_value * 10);
                break;
            case TinyLiveDataField::MinCellMv:
                min_cell_mv = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::MaxCellMv:
                max_cell_mv = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::BalancingBits:
                balancing_bits = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::MaxChargeCurrent:
                max_charge_current = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::MaxDischargeCurrent:
                max_discharge_current = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::OnlineStatus:
                online_status = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::NeedBalancing:
                // reserved
                break;
            case TinyLiveDataField::CellImbalanceMv:
                cell_imbalance_mv = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::CellOvervoltageMv:
                cell_overvoltage_mv = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::CellUndervoltageMv:
                cell_undervoltage_mv = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::DischargeOvercurrentA:
                discharge_overcurrent_a = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::ChargeOvercurrentA:
                charge_overcurrent_a = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::OverheatCutoffC:
                overheat_cutoff_c = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::None:
            default:
                break;
        }
    }

    void applyBinding(const TinyRegisterRuntimeBinding& binding,
                      int32_t raw_value,
                      float scaled_value,
                      const String* text_value,
                      const uint16_t* words_buffer) {
        applyField(binding.live_field, scaled_value, raw_value);

        if (binding.live_field == TinyLiveDataField::PackMinTemperature && words_buffer != nullptr &&
            binding.register_count > 0) {
            const uint16_t word = words_buffer[0];
            const uint8_t high_byte = static_cast<uint8_t>((word >> 8) & 0xFFu);
            const int8_t signed_high = static_cast<int8_t>(high_byte);
            pack_temp_max = static_cast<int16_t>(signed_high) * 10;
        }

        appendSnapshot(binding.metadata_address,
                       binding.value_type,
                       raw_value,
                       binding.register_count,
                       text_value,
                       words_buffer);
    }
};

// ====================================================================================
// OUTILS DE LOG (FACULTATIFS)
// ====================================================================================

#ifdef LOGGER_AVAILABLE
#include "logger.h"
extern Logger logger;

/**
 * @brief Macro pour journaliser un snapshot de données BMS
 * 
 * Exemple :
 * ```
 * TinyBMS_LiveData data;
 * LOG_LIVEDATA(data, LOG_DEBUG);
 * ```
 */
#define LOG_LIVEDATA(data, level) \
    do { \
        if (logger.getLevel() >= level) { \
            logger.log(level, (data).toString()); \
        } \
    } while (0)
#else
/**
 * @brief Stub vide si logger non disponible (compilation sans logs)
 */
#define LOG_LIVEDATA(data, level) do {} while (0)
#endif

#endif // SHARED_DATA_H
