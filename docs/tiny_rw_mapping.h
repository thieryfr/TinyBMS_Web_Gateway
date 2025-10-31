#pragma once

#include <Arduino.h>
#include <vector>

class Logger;

#ifndef ARDUINO
namespace fs {
    class FS;
}
#else
#include <FS.h>
#endif

enum class TinyRegisterAccess : uint8_t {
    ReadOnly = 0,
    WriteOnly,
    ReadWrite
};

enum class TinyRegisterValueClass : uint8_t {
    Unknown = 0,
    Uint,
    Int,
    Float,
    Enum
};

struct TinyRegisterEnumOption {
    uint16_t value = 0;
    String label;
};

struct TinyRwRegisterMetadata {
    uint16_t address = 0;
    TinyRegisterAccess access = TinyRegisterAccess::ReadWrite;
    TinyRegisterValueClass value_class = TinyRegisterValueClass::Unknown;
    String key;
    String label;
    String unit;
    String type;
    String group;
    String comment;
    float scale = 1.0f;
    float offset = 0.0f;
    float step = 1.0f;
    uint8_t precision = 0;
    bool has_min = false;
    float min_value = 0.0f;
    bool has_max = false;
    float max_value = 0.0f;
    float default_value = 0.0f;
    uint16_t default_raw = 0;
    std::vector<TinyRegisterEnumOption> enum_values;
};

bool initializeTinyRwMapping(fs::FS& fs, const char* path, Logger* logger = nullptr);

bool loadTinyRwMappingFromJson(const char* json, Logger* logger = nullptr);

const std::vector<TinyRwRegisterMetadata>& getTinyRwRegisters();

const TinyRwRegisterMetadata* findTinyRwRegister(uint16_t address);

const TinyRwRegisterMetadata* findTinyRwRegisterByKey(const String& key);

float tinyRwConvertRawToUser(const TinyRwRegisterMetadata& meta, uint16_t raw_value);

bool tinyRwConvertUserToRaw(const TinyRwRegisterMetadata& meta, float user_value, uint16_t& raw_out);
