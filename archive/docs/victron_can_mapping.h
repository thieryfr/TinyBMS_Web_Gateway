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

#include "tiny_read_mapping.h"

enum class VictronValueSourceType : uint8_t {
    Unknown = 0,
    LiveData,
    Function,
    Constant
};

enum class VictronFieldEncoding : uint8_t {
    Unsigned = 0,
    Signed,
    Bits
};

enum class VictronFieldEndianness : uint8_t {
    Little = 0,
    Big
};

struct VictronFieldConversion {
    float gain = 1.0f;
    float offset = 0.0f;
    bool round = false;
    bool has_min = false;
    bool has_max = false;
    float min_value = 0.0f;
    float max_value = 0.0f;
};

struct VictronValueSource {
    VictronValueSourceType type = VictronValueSourceType::Unknown;
    TinyLiveDataField live_field = TinyLiveDataField::None;
    String identifier;  // live field name or function id for diagnostics
    float constant = 0.0f;
};

struct VictronCanFieldDefinition {
    String name;
    uint8_t byte_offset = 0;
    uint8_t length = 0;        // bytes for integer encodings
    uint8_t bit_offset = 0;    // for bit-field encodings (0..7)
    uint8_t bit_length = 0;    // number of bits when encoding == Bits
    VictronFieldEncoding encoding = VictronFieldEncoding::Unsigned;
    VictronFieldEndianness endianness = VictronFieldEndianness::Little;
    VictronValueSource source;
    VictronFieldConversion conversion;
};

struct VictronPgnDefinition {
    uint16_t pgn = 0;
    String name;
    std::vector<VictronCanFieldDefinition> fields;
};

bool initializeVictronCanMapping(fs::FS& fs, const char* path, Logger* logger = nullptr);
bool loadVictronCanMappingFromJson(const char* json, Logger* logger = nullptr);

const std::vector<VictronPgnDefinition>& getVictronPgnDefinitions();
const VictronPgnDefinition* findVictronPgnDefinition(uint16_t pgn);

String victronValueSourceTypeToString(VictronValueSourceType type);
String tinyLiveDataFieldToString(TinyLiveDataField field);
String victronFieldEncodingToString(VictronFieldEncoding encoding);

