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

enum class TinyRegisterValueType : uint8_t {
    Unknown = 0,
    Uint8,
    Uint16,
    Uint32,
    Int8,
    Int16,
    Float,
    String
};

enum class TinyRegisterDataSlice : uint8_t {
    FullWord = 0,
    LowByte,
    HighByte
};

enum class TinyLiveDataField : uint8_t {
    None = 0,
    Voltage,
    Current,
    SocPercent,
    SohPercent,
    Temperature,
    MinCellMv,
    MaxCellMv,
    BalancingBits,
    MaxChargeCurrent,
    MaxDischargeCurrent,
    OnlineStatus,
    NeedBalancing,
    CellImbalanceMv,
    PackMinTemperature,
    PackMaxTemperature,
    CellOvervoltageMv,
    CellUndervoltageMv,
    DischargeOvercurrentA,
    ChargeOvercurrentA,
    OverheatCutoffC
};

struct TinyRegisterMetadata {
    uint16_t primary_address = 0;
    std::vector<uint16_t> addresses;
    TinyRegisterValueType type = TinyRegisterValueType::Unknown;
    String name;
    String unit;
    String comment;
    String raw_key;
};

struct TinyRegisterRuntimeBinding {
    uint16_t register_address = 0;
    uint8_t register_count = 1;
    uint16_t metadata_address = 0;
    TinyRegisterValueType value_type = TinyRegisterValueType::Unknown;
    bool is_signed = false;
    float scale = 1.0f;
    TinyLiveDataField live_field = TinyLiveDataField::None;
    const char* fallback_name = nullptr;
    const char* fallback_unit = nullptr;
    const TinyRegisterMetadata* metadata = nullptr;
    TinyRegisterDataSlice data_slice = TinyRegisterDataSlice::FullWord;
};

bool initializeTinyReadMapping(fs::FS& fs, const char* path, Logger* logger = nullptr);

bool loadTinyReadMappingFromJson(const char* json, Logger* logger = nullptr);

const std::vector<TinyRegisterMetadata>& getTinyRegisterMetadata();
const TinyRegisterMetadata* findTinyRegisterMetadata(uint16_t address);

const std::vector<TinyRegisterRuntimeBinding>& getTinyRegisterBindings();
const TinyRegisterRuntimeBinding* findTinyRegisterBinding(uint16_t address);

String tinyRegisterTypeToString(TinyRegisterValueType type);

