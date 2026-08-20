// include/industrial_config_engine/l1_action.hpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <memory>
#include <regex>
#include <nlohmann/json.hpp>
#include "types.hpp"
#include <iostream>

namespace industrial_config_engine {

    // ============================================================
    // Argument definition
    // ============================================================
    struct ArgDef {
        int index = 0;
        std::string name;
        DataType type = DataType::VOIDDataType;
        std::optional<int64_t> min_val;
        std::optional<int64_t> max_val;
        std::optional<double> min_float;
        std::optional<double> max_float;
        std::optional<std::string> default_value;
        std::vector<std::string> enum_values;
        std::string desc;

        bool validate(const nlohmann::json& value) const;
        std::string getTypeName() const { return dataTypeToString(type); }
    };

    // ============================================================
    // Parse configuration (Modbus read register)
    // ============================================================
    struct ParseConfig {
        int start_byte = 0;
        int length = 0;
        std::string endian = "big";
        std::string type = "uint16";

        bool hasPlaceholders() const;
        std::vector<std::string> getPlaceholders() const;
    };

    // ============================================================
    // Check condition configuration
    // ============================================================
    struct CheckConfig {
        std::string condition;
        std::string value;

        bool hasPlaceholders() const;
        std::vector<std::string> getPlaceholders() const;
    };

    // ============================================================
    // Action signature
    // ============================================================
    struct ActionSignature {
        std::string name;
        std::vector<DataType> params;
        DataType return_type = DataType::VOIDDataType;
        std::string result_key;

        std::string toString() const;
        bool validateParamCount(size_t count) const;
        bool validateParamTypes(const std::vector<DataType>& types) const;
    };

    // ============================================================
    // L1Action class
    // ============================================================
    class L1Action {
    public:
        L1Action() = default;
        explicit L1Action(const nlohmann::json& json);
        explicit L1Action(const std::string& filename, const nlohmann::json& json);
        explicit L1Action(const std::string& filename, const std::string& json_str);

        // Loading
        bool loadFromJson(const nlohmann::json& json);
        bool loadFromJsonString(const std::string& json_str);
        bool loadFromFile(const std::string& filepath);

        // Export
        nlohmann::json toJson() const;
        std::string toJsonString(bool pretty = true) const;

        // Filename parsing
        static ActionSignature parseSignatureFromFilename(const std::string& filename);
        static bool isValidFilename(const std::string& filename);
        static std::string generateFilename(const std::string& name,
            const std::vector<DataType>& params,
            DataType return_type,
            const std::string& result_key = "");

        // Placeholder handling
        std::vector<std::string> extractPlaceholders() const;
        std::string replacePlaceholders(const std::string& template_str,
            const std::unordered_map<std::string, std::string>& values) const;
        L1Action resolvePlaceholders(const std::unordered_map<std::string, std::string>& values) const;

        // Argument validation
        bool validateArgValue(const std::string& arg_name, const nlohmann::json& value) const;
        bool validateArgValue(int index, const nlohmann::json& value) const;
        bool validateArgValues(const std::vector<nlohmann::json>& values) const;
        const ArgDef* getArgByName(const std::string& name) const;
        const ArgDef* getArgByIndex(int index) const;

        // Type checking
        bool checkParamTypes(const std::vector<DataType>& types) const;
        std::vector<DataType> getExpectedParamTypes() const;

        // Getters
        const std::string& getFilename() const { return filename_; }
        const std::string& getType() const { return type_; }
        const std::string& getDescription() const { return description_; }
        const std::string& getProtocol() const { return protocol_; }
        const std::string& getRequest() const { return request_; }
        const std::string& getResponse() const { return response_; }
        int getTimeoutMs() const { return timeout_ms_; }
        const std::string& getTimeoutExpr() const { return timeout_expr_; }
        const std::vector<ArgDef>& getArgs() const { return args_; }
        const ActionSignature& getSignature() const { return signature_; }

        // ✅ Return a const reference rather than the optional itself
        const ParseConfig* getParse() const { return parse_.has_value() ? &parse_.value() : nullptr; }
        const CheckConfig* getCheck() const { return check_.has_value() ? &check_.value() : nullptr; }

        const std::optional<std::string>& getExpression() const { return expression_; }
        const std::optional<std::string>& getDuration() const { return duration_; }
        const std::string& getUnit() const { return unit_; }
        bool isInitialized() const { return initialized_; }

        // Convenience predicates
        bool hasParse() const { return parse_.has_value(); }
        bool hasCheck() const { return check_.has_value(); }
        bool hasExpression() const { return expression_.has_value(); }
        bool hasDuration() const { return duration_.has_value(); }
        bool hasResponse() const { return !response_.empty(); }
        bool hasTimeoutExpr() const { return !timeout_expr_.empty(); }
        bool hasArgs() const { return !args_.empty(); }
        bool hasReturnValue() const { return signature_.return_type != DataType::VOIDDataType; }

        // Action type predicates
        bool isModbusWriteVerify() const { return type_ == "modbus_write_verify"; }
        bool isModbusReadCache() const { return type_ == "modbus_read_cache"; }
        bool isModbusReadCheck() const { return type_ == "modbus_read_check"; }
        bool isWait() const { return type_ == "wait"; }
        bool isCalculate() const { return type_ == "calculate"; }
        bool isCalculateCheck() const { return type_ == "calculate_check"; }
        bool isSetVariable() const { return type_ == "set_variable"; }
        bool isReadVariable() const { return type_ == "read_variable"; }
        bool isUiAction() const { return type_ == "ui_action"; }
        bool isUserDecision() const { return type_ == "user_decision"; }
        bool isLog() const { return type_ == "log"; }
        bool isPopup() const { return type_ == "popup"; }
        bool isScriptExec() const { return type_ == "script_exec"; }

        // Debugging
        std::string toString() const;
        void print(std::ostream& os = std::cout) const;

    private:
        void parseCommonFields(const nlohmann::json& json);
        void parseArgs(const nlohmann::json& json);
        void parseParseConfig(const nlohmann::json& json);
        void parseCheckConfig(const nlohmann::json& json);
        void parseExpression(const nlohmann::json& json);
        void parseDuration(const nlohmann::json& json);

        std::vector<std::string> extractPlaceholdersFromString(const std::string& str) const;
        bool hasPlaceholdersInString(const std::string& str) const;

    private:
        std::string filename_;
        std::string filepath_;
        std::string type_;
        std::string description_;
        std::string protocol_;
        std::string request_;
        std::string response_;
        int timeout_ms_ = 0;
        std::string timeout_expr_;
        std::vector<ArgDef> args_;
        std::optional<ParseConfig> parse_;
        std::optional<CheckConfig> check_;
        std::optional<std::string> expression_;
        std::optional<std::string> duration_;
        std::string unit_ = "seconds";
        ActionSignature signature_;
        bool initialized_ = false;
    };

    std::ostream& operator<<(std::ostream& os, const L1Action& action);

} // namespace industrial_config_engine