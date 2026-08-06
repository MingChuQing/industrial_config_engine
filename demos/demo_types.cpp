// demos/demo_types.cpp
#include <iostream>
#include <nlohmann/json.hpp>
#include "industrial_config_engine/types.hpp"

using namespace industrial_config_engine;

int main() {
    std::cout << "=== Type System Demo ===" << std::endl;

    // 1. Type conversion
    std::cout << "\n1. Type conversion:" << std::endl;
    std::cout << "  string 'u16' -> DataType: "
        << static_cast<int>(stringToDataType("u16")) << std::endl;
    std::cout << "  DataType::U16 -> string: "
        << dataTypeToString(DataType::U16) << std::endl;

    // 2. Type information
    std::cout << "\n2. Type information:" << std::endl;
    auto info = getTypeInfo(DataType::U16);
    std::cout << "  Name: " << info.name << std::endl;
    std::cout << "  Full name: " << info.full_name << std::endl;
    std::cout << "  Size: " << (int)info.size << " bytes" << std::endl;
    std::cout << "  Description: " << info.description << std::endl;

    // 3. Type checking
    std::cout << "\n3. Type checking:" << std::endl;
    std::cout << "  U16 is numeric: " << (isNumericType(DataType::U16) ? "Yes" : "No") << std::endl;
    std::cout << "  U16 is unsigned: " << (isUnsignedType(DataType::U16) ? "Yes" : "No") << std::endl;
    std::cout << "  STRING is string type: " << (isStringType(DataType::S) ? "Yes" : "No") << std::endl;

    // 4. Type validation
    std::cout << "\n4. Type validation:" << std::endl;
    std::cout << "  Value 300 in U16 range: "
        << (validateValue<uint16_t>(DataType::U16, 300) ? "Valid" : "Invalid") << std::endl;
    std::cout << "  Value 70000 in U16 range: "
        << (validateValue<uint16_t>(DataType::U16, 70000) ? "Valid" : "Invalid") << std::endl;

    // 5. HEX validation
    std::cout << "\n5. HEX validation:" << std::endl;
    std::cout << "  '01 02 FF' is valid: "
        << (isValidHexString("01 02 FF") ? "Valid" : "Invalid") << std::endl;
    std::cout << "  '01 02 GG' is valid: "
        << (isValidHexString("01 02 GG") ? "Valid" : "Invalid") << std::endl;

    // 6. Condition operators
    std::cout << "\n6. Condition operators:" << std::endl;
    std::cout << "  'le' -> " << conditionToString(stringToCondition("le")) << std::endl;
    std::cout << "  'gt' is valid: " << (isValidCondition("gt") ? "Yes" : "No") << std::endl;
    std::cout << "  'xx' is valid: " << (isValidCondition("xx") ? "Yes" : "No") << std::endl;

    // 7. Type list parsing
    std::cout << "\n7. Type list parsing:" << std::endl;
    std::string type_str = "u8_s_u16_b";
    auto types = parseTypeList(type_str);
    std::cout << "  Parsed '" << type_str << "' -> " << typeListToDebugString(types) << std::endl;

    // 8. All supported types
    std::cout << "\n8. All supported types:" << std::endl;
    std::vector<DataType> all_types = {
        DataType::U8, DataType::I8, DataType::U16, DataType::I16,
        DataType::U32, DataType::I32, DataType::F, DataType::B,
        DataType::S, DataType::HEX, DataType::ARR, DataType::VOIDDataType
    };
    for (auto type : all_types) {
        auto info = getTypeInfo(type);
        std::cout << "  " << info.name << " (" << info.full_name << ")" << std::endl;
    }

    return 0;
}