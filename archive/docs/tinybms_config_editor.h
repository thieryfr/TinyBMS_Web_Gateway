/**
 * @file tinybms_config_editor.h
 * @brief TinyBMS Configuration Editor Module with Logging + FreeRTOS
 * @version 1.1 - Integration with logger and structured feedback
 */

#ifndef TINYBMS_CONFIG_EDITOR_H
#define TINYBMS_CONFIG_EDITOR_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Freertos.h>
#include "watchdog_manager.h"
#include "rtos_tasks.h"
#include "tiny_rw_mapping.h"
#include "tinybms_victron_bridge.h"

extern WatchdogManager Watchdog;

// ✅ Optional logger integration
#ifdef LOGGER_AVAILABLE
#include "logger.h"
extern Logger logger;
#define CONFIG_LOG(level, msg) logger.log(level, String("[CONFIG_EDITOR] ") + msg)
#else
#define CONFIG_LOG(level, msg) Serial.println(String("[CONFIG_EDITOR] ") + msg)
#endif

/**
 * @brief Error codes returned by the configuration editor
 */
enum class TinyBMSConfigError : uint8_t {
    None = 0,
    MutexUnavailable,
    RegisterNotFound,
    OutOfRange,
    Timeout,
    WriteFailed,
    BridgeUnavailable,
    HardwareError
};

/**
 * @brief Result returned by configuration write operations
 */
struct TinyBMSConfigResult {
    TinyBMSConfigError error = TinyBMSConfigError::None;
    String message;

    bool ok() const { return error == TinyBMSConfigError::None; }
};

const char* tinybmsConfigErrorToString(TinyBMSConfigError error);

/**
 * @brief TinyBMS Configuration Register Definition
 */
struct TinyBMSConfigRegister {
    uint16_t address;
    String key;
    String description;
    String group;
    String unit;
    String type;
    String comment;
    TinyRegisterAccess access;
    TinyRegisterValueClass value_class;
    bool has_min;
    float min_value;
    bool has_max;
    float max_value;
    float scale;
    float offset;
    float step;
    uint8_t precision;
    uint16_t default_raw_value;
    float default_user_value;
    uint16_t current_raw_value;
    float current_user_value;
    bool is_enum;
    struct EnumOption {
        uint16_t value;
        String label;
    };
    EnumOption enum_values[16];
    uint8_t enum_count;
};

/**
 * @brief TinyBMS Configuration Editor Class
 */
class TinyBMSConfigEditor {
public:
    TinyBMSConfigEditor();

    void begin();
    String getRegistersJSON();
    bool readRegister(uint16_t address, float &value);
    bool readRegisterRaw(uint16_t address, uint16_t &value);
    TinyBMSConfigError writeRegister(uint16_t address, float value);
    TinyBMSConfigError writeRegisterRaw(uint16_t address, uint16_t value);
    uint8_t readAllRegisters();
    const TinyBMSConfigRegister *getRegister(uint16_t address) const;
    TinyBMSConfigResult writeConfig(const TinyBMS_Config &cfg);

private:
    static const uint8_t MAX_REGISTERS = 64;
    TinyBMSConfigRegister registers_[MAX_REGISTERS];
    uint8_t registers_count_;

    int8_t findRegisterIndex(uint16_t address) const;
    int8_t findRegisterIndexByKey(const String &key) const;
    void initializeRegisters();
    bool convertUserToRaw(const TinyBMSConfigRegister &reg, float user_value, uint16_t &raw) const;
    float convertRawToUser(const TinyBMSConfigRegister &reg, uint16_t raw) const;
    TinyBMSConfigError validateValue(const TinyBMSConfigRegister &reg, float user_value) const;
};

#endif // TINYBMS_CONFIG_EDITOR_H
