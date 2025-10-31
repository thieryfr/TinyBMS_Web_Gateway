#pragma once

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <type_traits>

// Minimal Arduino compatibility header used for host/unit-test builds.

constexpr unsigned char DEC = 10;
constexpr unsigned char HEX = 16;
constexpr unsigned char OCT = 8;
constexpr unsigned char BIN = 2;

class String {
public:
    String() = default;

    String(const char* cstr)
    {
        if (cstr != nullptr) {
            value_ = cstr;
        }
    }

    String(const std::string& value) : value_(value) {}

    String(char ch) : value_(1, ch) {}

    String(int value, unsigned char base = DEC) { value_ = formatInteger<long long>(value, base); }
    String(unsigned int value, unsigned char base = DEC) { value_ = formatInteger<unsigned long long>(value, base); }
    String(long value, unsigned char base = DEC) { value_ = formatInteger<long long>(value, base); }
    String(unsigned long value, unsigned char base = DEC) { value_ = formatInteger<unsigned long long>(value, base); }
    String(long long value, unsigned char base = DEC) { value_ = formatInteger<long long>(value, base); }
    String(unsigned long long value, unsigned char base = DEC) { value_ = formatInteger<unsigned long long>(value, base); }
    String(float value, unsigned char decimals = 2) { value_ = formatFloat(static_cast<double>(value), decimals); }
    String(double value, unsigned char decimals = 2) { value_ = formatFloat(value, decimals); }

    size_t length() const { return value_.size(); }
    bool isEmpty() const { return value_.empty(); }
    void reserve(size_t n) { value_.reserve(n); }
    void clear() { value_.clear(); }

    const char* c_str() const { return value_.c_str(); }

    String& operator=(const char* cstr)
    {
        value_ = (cstr != nullptr) ? cstr : "";
        return *this;
    }

    String& operator=(const std::string& str)
    {
        value_ = str;
        return *this;
    }

    String& operator+=(const String& other)
    {
        value_ += other.value_;
        return *this;
    }

    String& operator+=(const char* other)
    {
        if (other != nullptr) {
            value_ += other;
        }
        return *this;
    }

    String& operator+=(char ch)
    {
        value_.push_back(ch);
        return *this;
    }

    template <typename T,
              typename = typename std::enable_if<std::is_arithmetic<T>::value>::type>
    String& operator+=(T number)
    {
        value_ += String(number).value_;
        return *this;
    }

    String operator+(const String& other) const
    {
        return String(value_ + other.value_);
    }

    String operator+(const char* other) const
    {
        return String(value_ + (other != nullptr ? other : ""));
    }

    template <typename T,
              typename = typename std::enable_if<std::is_arithmetic<T>::value>::type>
    String operator+(T number) const
    {
        String result(*this);
        result += number;
        return result;
    }

    friend String operator+(const char* lhs, const String& rhs)
    {
        return String((lhs != nullptr ? lhs : "") + rhs.value_);
    }

    bool operator==(const String& other) const { return value_ == other.value_; }
    bool operator!=(const String& other) const { return value_ != other.value_; }

    operator std::string() const { return value_; }

private:
    template <typename Integer>
    static std::string formatInteger(Integer value, unsigned char base)
    {
        std::ostringstream oss;
        if (base == HEX) {
            if (std::is_signed<Integer>::value) {
                oss << std::uppercase << std::hex << static_cast<long long>(value);
            } else {
                oss << std::uppercase << std::hex << static_cast<unsigned long long>(value);
            }
        } else if (base == OCT) {
            if (std::is_signed<Integer>::value) {
                oss << std::oct << static_cast<long long>(value);
            } else {
                oss << std::oct << static_cast<unsigned long long>(value);
            }
        } else if (base == BIN) {
            using Unsigned = typename std::make_unsigned<Integer>::type;
            Unsigned unsigned_value = static_cast<Unsigned>(value);
            if (unsigned_value == 0) {
                return "0";
            }
            std::string bits;
            while (unsigned_value > 0) {
                bits.push_back((unsigned_value & 1U) ? '1' : '0');
                unsigned_value >>= 1U;
            }
            std::reverse(bits.begin(), bits.end());
            if (std::is_signed<Integer>::value && value < 0) {
                bits.insert(bits.begin(), '-');
            }
            return bits;
        } else {  // default decimal
            oss << static_cast<long double>(value);
        }
        return oss.str();
    }

    static std::string formatFloat(double value, unsigned char decimals)
    {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(decimals) << value;
        return oss.str();
    }

    std::string value_;
};

#define F(str) str

