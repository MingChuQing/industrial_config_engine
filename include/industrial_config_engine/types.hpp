// include/industrial_config_engine/types.hpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <cstdint>
#include <algorithm>
#include <regex>
#include <nlohmann/json.hpp>


namespace industrial_config_engine {

    // ============================================================
    // Data type enumeration
    // ============================================================
    enum class DataType : uint8_t {
        VOIDDataType = 0,
        U8,
        I8,
        U16,
        I16,
        U32,
        I32,
        F,
        B,
        S,
        HEX,
        ARR
    };

    // ============================================================
    // Type information structure
    // ============================================================
    struct TypeInfo {
        DataType type;
        std::string name;
        std::string full_name;
        uint8_t size;
        std::string description;
    };

    // ============================================================
    // Type conversion functions
    // ============================================================

    inline DataType stringToDataType(const std::string& str) {
        static const std::unordered_map<std::string, DataType> map = {
            {"void", DataType::VOIDDataType},
            {"u8", DataType::U8},
            {"i8", DataType::I8},
            {"u16", DataType::U16},
            {"i16", DataType::I16},
            {"u32", DataType::U32},
            {"i32", DataType::I32},
            {"f", DataType::F},
            {"b", DataType::B},
            {"s", DataType::S},
            {"hex", DataType::HEX},
            {"arr", DataType::ARR}
        };

        auto it = map.find(str);
        if (it != map.end()) {
            return it->second;
        }
        return DataType::VOIDDataType;
    }

    inline std::string dataTypeToString(DataType type) {
        static const std::unordered_map<DataType, std::string> map = {
            {DataType::VOIDDataType, "void"},
            {DataType::U8, "u8"},
            {DataType::I8, "i8"},
            {DataType::U16, "u16"},
            {DataType::I16, "i16"},
            {DataType::U32, "u32"},
            {DataType::I32, "i32"},
            {DataType::F, "f"},
            {DataType::B, "b"},
            {DataType::S, "s"},
            {DataType::HEX, "hex"},
            {DataType::ARR, "arr"}
        };

        auto it = map.find(type);
        if (it != map.end()) {
            return it->second;
        }
        return "void";
    }

    // ✅ Added the missing function
    inline bool isValidDataType(const std::string& str) {
        static const std::vector<std::string> valid_types = {
            "void", "u8", "i8", "u16", "i16", "u32", "i32",
            "f", "b", "s", "hex", "arr"
        };
        return std::find(valid_types.begin(), valid_types.end(), str) != valid_types.end();
    }

    inline TypeInfo getTypeInfo(DataType type) {
        static const std::unordered_map<DataType, TypeInfo> map = {
            {DataType::VOIDDataType, {DataType::VOIDDataType, "void", "void", 0, "no return value"}},
            {DataType::U8, {DataType::U8, "u8", "uint8", 1, "0 ~ 255"}},
            {DataType::I8, {DataType::I8, "i8", "int8", 1, "-128 ~ 127"}},
            {DataType::U16, {DataType::U16, "u16", "uint16", 2, "0 ~ 65535"}},
            {DataType::I16, {DataType::I16, "i16", "int16", 2, "-32768 ~ 32767"}},
            {DataType::U32, {DataType::U32, "u32", "uint32", 4, "0 ~ 4294967295"}},
            {DataType::I32, {DataType::I32, "i32", "int32", 4, "-2147483648 ~ 2147483647"}},
            {DataType::F, {DataType::F, "f", "float32", 4, "±3.4e38"}},
            {DataType::B, {DataType::B, "b", "bool", 0, "true / false"}},
            {DataType::S, {DataType::S, "s", "string", 0, "arbitrary string"}},
            {DataType::HEX, {DataType::HEX, "hex", "hex_string", 0, "hex string"}},
            {DataType::ARR, {DataType::ARR, "arr", "array", 0, "JSON array"}}
        };

        auto it = map.find(type);
        if (it != map.end()) {
            return it->second;
        }
        return { DataType::VOIDDataType, "void", "void", 0, "unknown type" };
    }

    // ============================================================
    // Type predicate functions
    // ============================================================

    inline bool isNumericType(DataType type) {
        return type == DataType::U8 || type == DataType::I8 ||
            type == DataType::U16 || type == DataType::I16 ||
            type == DataType::U32 || type == DataType::I32 ||
            type == DataType::F;
    }

    inline bool isIntegerType(DataType type) {
        return type == DataType::U8 || type == DataType::I8 ||
            type == DataType::U16 || type == DataType::I16 ||
            type == DataType::U32 || type == DataType::I32;
    }

    inline bool isUnsignedType(DataType type) {
        return type == DataType::U8 || type == DataType::U16 || type == DataType::U32;
    }

    inline bool isSignedType(DataType type) {
        return type == DataType::I8 || type == DataType::I16 || type == DataType::I32;
    }

    inline bool isFloatType(DataType type) {
        return type == DataType::F;
    }

    inline bool isBoolType(DataType type) {
        return type == DataType::B;
    }

    inline bool isStringType(DataType type) {
        return type == DataType::S || type == DataType::HEX;
    }

    inline bool isArrayType(DataType type) {
        return type == DataType::ARR;
    }

    inline bool isVoidType(DataType type) {
        return type == DataType::VOIDDataType;
    }

    inline int getTypeSize(DataType type) {
        auto info = getTypeInfo(type);
        return info.size;
    }

    // ============================================================
    // Type validation functions
    // ============================================================

    template<typename T>
    bool validateValue(DataType type, const T& value) {
        switch (type) {
        case DataType::U8:
            return value >= 0 && value <= 255;
        case DataType::I8:
            return value >= -128 && value <= 127;
        case DataType::U16:
            return value >= 0 && value <= 65535;
        case DataType::I16:
            return value >= -32768 && value <= 32767;
        case DataType::U32:
            return value >= 0 && value <= 4294967295LL;
        case DataType::I32:
            return value >= -2147483648LL && value <= 2147483647LL;
        case DataType::F:
            return true;
        default:
            return true;
        }
    }

    inline bool isValidHexString(const std::string& str) {
        static const std::regex hex_pattern(R"(^([0-9A-Fa-f]{2}\s)*[0-9A-Fa-f]{2}$)");
        return std::regex_match(str, hex_pattern);
    }

    inline bool validateStringByType(const std::string& str, DataType type) {
        switch (type) {
        case DataType::S:
            return true;
        case DataType::HEX:
            return isValidHexString(str);
        default:
            return false;
        }
    }

    // ============================================================
    // Type comparison
    // ============================================================

    inline bool isTypeCompatible(DataType from, DataType to) {
        if (from == to) return true;

        if (isNumericType(from) && isNumericType(to)) {
            return true;
        }

        if (isIntegerType(from) && isIntegerType(to)) {
            return true;
        }

        if (from == DataType::B && to == DataType::U8) return true;
        if (from == DataType::U8 && to == DataType::B) return true;

        return false;
    }

    // ============================================================
    // Type parsing helpers
    // ============================================================

    inline std::vector<DataType> parseTypeList(const std::string& type_str) {
        std::vector<DataType> result;
        if (type_str.empty()) {
            return result;
        }

        size_t pos = 0;
        size_t next_pos = 0;
        while ((next_pos = type_str.find('_', pos)) != std::string::npos) {
            std::string token = type_str.substr(pos, next_pos - pos);
            if (!token.empty()) {
                result.push_back(stringToDataType(token));
            }
            pos = next_pos + 1;
        }
        if (pos < type_str.length()) {
            std::string token = type_str.substr(pos);
            if (!token.empty()) {
                result.push_back(stringToDataType(token));
            }
        }

        return result;
    }

    inline std::string typeListToString(const std::vector<DataType>& types) {
        std::string result;
        for (size_t i = 0; i < types.size(); ++i) {
            if (i > 0) result += "_";
            result += dataTypeToString(types[i]);
        }
        return result;
    }

    // ============================================================
    // Type check (for JSON parsing)
    // ============================================================

    inline bool isJsonTypeMatch(const nlohmann::json& value, DataType expected_type) {
        switch (expected_type) {
        case DataType::U8:
        case DataType::I8:
        case DataType::U16:
        case DataType::I16:
        case DataType::U32:
        case DataType::I32:
            return value.is_number_integer();
        case DataType::F:
            return value.is_number();
        case DataType::B:
            return value.is_boolean();
        case DataType::S:
        case DataType::HEX:
            return value.is_string();
        case DataType::ARR:
            return value.is_array();
        default:
            return false;
        }
    }

    inline std::string getJsonTypeName(const nlohmann::json& value) {
        if (value.is_null()) return "null";
        if (value.is_boolean()) return "boolean";
        if (value.is_number_integer()) return "integer";
        if (value.is_number_float()) return "float";
        if (value.is_string()) return "string";
        if (value.is_array()) return "array";
        if (value.is_object()) return "object";
        return "unknown";
    }

    // ============================================================
    // Condition operator enumeration
    // ============================================================

    enum class ConditionOperator {
        EQ, NE, LT, LE, GT, GE
    };

    inline ConditionOperator stringToCondition(const std::string& str) {
        static const std::unordered_map<std::string, ConditionOperator> map = {
            {"eq", ConditionOperator::EQ},
            {"ne", ConditionOperator::NE},
            {"lt", ConditionOperator::LT},
            {"le", ConditionOperator::LE},
            {"gt", ConditionOperator::GT},
            {"ge", ConditionOperator::GE}
        };

        auto it = map.find(str);
        if (it != map.end()) {
            return it->second;
        }
        return ConditionOperator::EQ;
    }

    inline std::string conditionToString(ConditionOperator op) {
        static const std::unordered_map<ConditionOperator, std::string> map = {
            {ConditionOperator::EQ, "eq"},
            {ConditionOperator::NE, "ne"},
            {ConditionOperator::LT, "lt"},
            {ConditionOperator::LE, "le"},
            {ConditionOperator::GT, "gt"},
            {ConditionOperator::GE, "ge"}
        };

        auto it = map.find(op);
        if (it != map.end()) {
            return it->second;
        }
        return "eq";
    }

    inline bool isValidCondition(const std::string& condition) {
        static const std::vector<std::string> valid_conditions = {
            "eq", "ne", "lt", "le", "gt", "ge"
        };
        return std::find(valid_conditions.begin(), valid_conditions.end(), condition) != valid_conditions.end();
    }

    // ============================================================
    // Printing and debugging
    // ============================================================

    inline std::string typeListToDebugString(const std::vector<DataType>& types) {
        std::string result = "[";
        for (size_t i = 0; i < types.size(); ++i) {
            if (i > 0) result += ", ";
            result += dataTypeToString(types[i]);
        }
        result += "]";
        return result;
    }

    inline std::string typeToDebugString(DataType type) {
        auto info = getTypeInfo(type);
        return info.name + " (" + info.full_name + ", " + std::to_string(info.size) + " bytes)";
    }

} // namespace industrial_config_engine