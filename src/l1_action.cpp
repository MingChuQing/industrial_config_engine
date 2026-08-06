// src/l1_action.cpp
#include "industrial_config_engine/l1_action.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <iomanip>

namespace industrial_config_engine {

    // ============================================================
    // ArgDef 实现
    // ============================================================

    bool ArgDef::validate(const nlohmann::json& value) const {
        // 类型检查
        switch (type) {
        case DataType::U8:
        case DataType::I8:
        case DataType::U16:
        case DataType::I16:
        case DataType::U32:
        case DataType::I32:
            if (!value.is_number_integer()) return false;
            break;
        case DataType::F:
            if (!value.is_number()) return false;
            break;
        case DataType::B:
            if (!value.is_boolean()) return false;
            break;
        case DataType::S:
        case DataType::HEX:
            if (!value.is_string()) return false;
            break;
        case DataType::ARR:
            if (!value.is_array()) return false;
            break;
        default:
            return false;
        }

        // 范围检查
        if (value.is_number_integer()) {
            int64_t int_val = value.get<int64_t>();
            if (min_val.has_value() && int_val < min_val.value()) return false;
            if (max_val.has_value() && int_val > max_val.value()) return false;
        }

        if (value.is_number_float()) {
            double float_val = value.get<double>();
            if (min_float.has_value() && float_val < min_float.value()) return false;
            if (max_float.has_value() && float_val > max_float.value()) return false;
        }

        // 枚举检查
        if (!enum_values.empty() && value.is_string()) {
            std::string str_val = value.get<std::string>();
            if (std::find(enum_values.begin(), enum_values.end(), str_val) == enum_values.end()) {
                return false;
            }
        }

        // HEX格式检查
        if (type == DataType::HEX && value.is_string()) {
            std::string hex_str = value.get<std::string>();
            if (!isValidHexString(hex_str)) return false;
        }

        return true;
    }

    // ============================================================
    // ParseConfig 实现
    // ============================================================

    bool ParseConfig::hasPlaceholders() const {
        return !getPlaceholders().empty();
    }

    std::vector<std::string> ParseConfig::getPlaceholders() const {
        std::vector<std::string> result;
        // ParseConfig 中的字段可能包含占位符
        // 例如: start_byte 可能是 "${offset}"
        // 但目前设计为数值，暂不实现占位符提取
        return result;
    }

    // ============================================================
    // CheckConfig 实现
    // ============================================================

    bool CheckConfig::hasPlaceholders() const {
        return !getPlaceholders().empty();
    }

    std::vector<std::string> CheckConfig::getPlaceholders() const {
        std::vector<std::string> result;

        // 从 condition 中提取
        static const std::regex pattern(R"(\$\{([^}]+)\})");
        std::smatch match;
        std::string::const_iterator search_start(condition.cbegin());

        while (std::regex_search(search_start, condition.cend(), match, pattern)) {
            result.push_back(match[1].str());
            search_start = match.suffix().first;
        }

        // 从 value 中提取
        search_start = value.cbegin();
        while (std::regex_search(search_start, value.cend(), match, pattern)) {
            result.push_back(match[1].str());
            search_start = match.suffix().first;
        }

        // 去重
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());

        return result;
    }

    // ============================================================
    // ActionSignature 实现
    // ============================================================

    std::string ActionSignature::toString() const {
        std::string result = name + "(";
        for (size_t i = 0; i < params.size(); ++i) {
            if (i > 0) result += ", ";
            result += dataTypeToString(params[i]);
        }
        result += ") -> " + dataTypeToString(return_type);
        if (!result_key.empty()) {
            result += " [" + result_key + "]";
        }
        return result;
    }

    bool ActionSignature::validateParamCount(size_t count) const {
        return params.size() == count;
    }

    bool ActionSignature::validateParamTypes(const std::vector<DataType>& types) const {
        if (params.size() != types.size()) return false;
        for (size_t i = 0; i < params.size(); ++i) {
            if (params[i] != types[i]) return false;
        }
        return true;
    }

    // ============================================================
    // L1Action 构造和加载
    // ============================================================

    L1Action::L1Action(const nlohmann::json& json) {
        loadFromJson(json);
    }

    L1Action::L1Action(const std::string& filename, const nlohmann::json& json)
        : filename_(filename) {
        signature_ = parseSignatureFromFilename(filename);
        loadFromJson(json);
    }

    L1Action::L1Action(const std::string& filename, const std::string& json_str)
        : filename_(filename) {
        signature_ = parseSignatureFromFilename(filename);
        loadFromJsonString(json_str);
    }

    bool L1Action::loadFromJson(const nlohmann::json& json) {
        try {
            parseCommonFields(json);
            parseArgs(json);
            parseParseConfig(json);
            parseCheckConfig(json);
            parseExpression(json);
            parseDuration(json);

            // 如果签名未设置但有filename，重新解析
            if (signature_.name.empty() && !filename_.empty()) {
                signature_ = parseSignatureFromFilename(filename_);
            }

            initialized_ = true;
            return true;
        }
        catch (const std::exception& e) {
            initialized_ = false;
            return false;
        }
    }

    bool L1Action::loadFromJsonString(const std::string& json_str) {
        try {
            nlohmann::json json = nlohmann::json::parse(json_str);
            return loadFromJson(json);
        }
        catch (const std::exception& e) {
            initialized_ = false;
            return false;
        }
    }

    bool L1Action::loadFromFile(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return false;
        }

        nlohmann::json json;
        try {
            file >> json;
        }
        catch (const std::exception& e) {
            return false;
        }

        filepath_ = filepath;

        // 从路径提取文件名
        size_t pos = filepath.find_last_of("/\\");
        std::string basename = (pos != std::string::npos) ? filepath.substr(pos + 1) : filepath;
        pos = basename.find_last_of('.');
        filename_ = (pos != std::string::npos) ? basename.substr(0, pos) : basename;

        signature_ = parseSignatureFromFilename(filename_);
        return loadFromJson(json);
    }

    // ============================================================
    // 导出为JSON
    // ============================================================

    nlohmann::json L1Action::toJson() const {
        nlohmann::json json;

        if (!type_.empty()) json["type"] = type_;
        if (!description_.empty()) json["description"] = description_;
        if (!protocol_.empty()) json["protocol"] = protocol_;
        if (!request_.empty()) json["request"] = request_;
        if (!response_.empty()) json["response"] = response_;

        if (!timeout_expr_.empty()) {
            json["timeout_ms"] = timeout_expr_;
        }
        else {
            json["timeout_ms"] = timeout_ms_;
        }

        if (!args_.empty()) {
            nlohmann::json args_json = nlohmann::json::array();
            for (const auto& arg : args_) {
                nlohmann::json arg_json;
                arg_json["index"] = arg.index;
                arg_json["name"] = arg.name;
                arg_json["type"] = dataTypeToString(arg.type);
                if (!arg.desc.empty()) arg_json["desc"] = arg.desc;
                if (arg.min_val.has_value()) arg_json["min"] = arg.min_val.value();
                if (arg.max_val.has_value()) arg_json["max"] = arg.max_val.value();
                if (arg.default_value.has_value()) arg_json["default"] = arg.default_value.value();
                if (!arg.enum_values.empty()) arg_json["enum"] = arg.enum_values;
                args_json.push_back(arg_json);
            }
            json["args"] = args_json;
        }

        if (parse_.has_value()) {
            nlohmann::json parse_json;
            parse_json["start_byte"] = parse_->start_byte;
            parse_json["length"] = parse_->length;
            parse_json["endian"] = parse_->endian;
            parse_json["type"] = parse_->type;
            json["parse"] = parse_json;
        }

        if (check_.has_value()) {
            nlohmann::json check_json;
            check_json["condition"] = check_->condition;
            check_json["value"] = check_->value;
            json["check"] = check_json;
        }

        if (expression_.has_value()) {
            json["expression"] = expression_.value();
        }

        if (duration_.has_value()) {
            json["duration"] = duration_.value();
            json["unit"] = unit_;
        }

        return json;
    }

    std::string L1Action::toJsonString(bool pretty) const {
        nlohmann::json json = toJson();
        return pretty ? json.dump(4) : json.dump();
    }

    // ============================================================
    // 文件名解析（静态方法）
    // ============================================================

    ActionSignature L1Action::parseSignatureFromFilename(const std::string& filename) {
        ActionSignature sig;
        sig.return_type = DataType::VOIDDataType;
        sig.result_key = "";

        if (filename.empty()) {
            return sig;
        }

        // 查找 ".r_"
        size_t r_pos = filename.find(".r_");

        if (r_pos != std::string::npos) {
            // 提取返回值部分: "b_write_result"
            std::string return_part = filename.substr(r_pos + 3);
            size_t underscore_pos = return_part.find('_');
            if (underscore_pos != std::string::npos) {
                sig.return_type = stringToDataType(return_part.substr(0, underscore_pos));
                sig.result_key = return_part.substr(underscore_pos + 1);
            }
            else {
                sig.return_type = stringToDataType(return_part);
                sig.result_key = "";
            }

            // 提取前缀部分
            std::string prefix = filename.substr(0, r_pos);
            size_t dot_pos = prefix.find('.');
            if (dot_pos != std::string::npos) {
                sig.name = prefix.substr(0, dot_pos);
                std::string params_str = prefix.substr(dot_pos + 1);
                if (!params_str.empty()) {
                    sig.params = parseTypeList(params_str);
                }
            }
            else {
                sig.name = prefix;
            }
        }
        else {
            // 无返回值
            size_t dot_pos = filename.find('.');
            if (dot_pos != std::string::npos) {
                sig.name = filename.substr(0, dot_pos);
                std::string params_str = filename.substr(dot_pos + 1);
                if (!params_str.empty()) {
                    sig.params = parseTypeList(params_str);
                }
            }
            else {
                sig.name = filename;
            }
            sig.return_type = DataType::VOIDDataType;
            sig.result_key = "";
        }

        return sig;
    }

    bool L1Action::isValidFilename(const std::string& filename) {
        if (filename.empty()) return false;

        // 基本检查：必须包含名称
        size_t dot_pos = filename.find('.');
        if (dot_pos == std::string::npos || dot_pos == 0) {
            return false;
        }

        // 检查参数部分是否都是有效类型
        std::string params_part = filename.substr(dot_pos + 1);
        // 如果有 ".r_"，只检查前面的部分
        size_t r_pos = params_part.find(".r_");
        if (r_pos != std::string::npos) {
            params_part = params_part.substr(0, r_pos);
        }

        if (!params_part.empty()) {
            auto types = parseTypeList(params_part);
            for (auto type : types) {
                if (type == DataType::VOIDDataType) {
                    // 检查是否是有效的类型名
                    // 如果解析为VOIDDataType，可能是无效类型
                    std::string type_str = params_part.substr(0, params_part.find('_'));
                    if (!isValidDataType(type_str)) {
                        return false;
                    }
                }
            }
        }

        return true;
    }

    std::string L1Action::generateFilename(const std::string& name,
        const std::vector<DataType>& params,
        DataType return_type,
        const std::string& result_key) {
        std::string filename = name;

        // 添加参数
        if (!params.empty()) {
            filename += ".";
            filename += typeListToString(params);
        }

        // 添加返回值
        if (return_type != DataType::VOIDDataType) {
            filename += ".r_";
            filename += dataTypeToString(return_type);
            if (!result_key.empty()) {
                filename += "_" + result_key;
            }
        }

        return filename;
    }

    // ============================================================
    // 占位符处理
    // ============================================================

    std::vector<std::string> L1Action::extractPlaceholders() const {
        std::vector<std::string> result;

        auto addPlaceholders = [&](const std::string& str) {
            auto extracted = extractPlaceholdersFromString(str);
            result.insert(result.end(), extracted.begin(), extracted.end());
            };

        // 从各个字段提取
        addPlaceholders(request_);
        addPlaceholders(response_);
        addPlaceholders(timeout_expr_);

        if (expression_.has_value()) {
            addPlaceholders(expression_.value());
        }
        if (duration_.has_value()) {
            addPlaceholders(duration_.value());
        }
        if (check_.has_value()) {
            addPlaceholders(check_->condition);
            addPlaceholders(check_->value);
        }

        // 从参数默认值中提取
        for (const auto& arg : args_) {
            if (arg.default_value.has_value()) {
                addPlaceholders(arg.default_value.value());
            }
        }

        // 去重
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());

        return result;
    }

    std::string L1Action::replacePlaceholders(const std::string& template_str,
        const std::unordered_map<std::string, std::string>& values) const {
        std::string result = template_str;
        static const std::regex pattern(R"(\$\{([^}]+)\})");
        std::smatch match;
        std::string::const_iterator search_start(result.cbegin());

        while (std::regex_search(search_start, result.cend(), match, pattern)) {
            std::string placeholder = match[1].str();
            auto it = values.find(placeholder);
            if (it != values.end()) {
                std::string replacement = it->second;
                size_t pos = match.position(0) + (search_start - result.cbegin());
                result.replace(pos, match.length(0), replacement);
                search_start = result.cbegin() + pos + replacement.length();
            }
            else {
                search_start = match.suffix().first;
            }
        }

        return result;
    }

    L1Action L1Action::resolvePlaceholders(const std::unordered_map<std::string, std::string>& values) const {
        L1Action resolved = *this;

        // 替换请求
        resolved.request_ = replacePlaceholders(request_, values);
        resolved.response_ = replacePlaceholders(response_, values);

        // 替换超时
        if (!timeout_expr_.empty()) {
            std::string resolved_timeout = replacePlaceholders(timeout_expr_, values);
            try {
                resolved.timeout_ms_ = std::stoi(resolved_timeout);
                resolved.timeout_expr_.clear();
            }
            catch (...) {
                resolved.timeout_expr_ = resolved_timeout;
            }
        }

        // 替换表达式
        if (expression_.has_value()) {
            resolved.expression_ = replacePlaceholders(expression_.value(), values);
        }

        // 替换duration
        if (duration_.has_value()) {
            resolved.duration_ = replacePlaceholders(duration_.value(), values);
        }

        // 替换check
        if (check_.has_value()) {
            resolved.check_->condition = replacePlaceholders(check_->condition, values);
            resolved.check_->value = replacePlaceholders(check_->value, values);
        }

        return resolved;
    }

    std::vector<std::string> L1Action::extractPlaceholdersFromString(const std::string& str) const {
        std::vector<std::string> result;
        static const std::regex pattern(R"(\$\{([^}]+)\})");
        std::smatch match;
        std::string::const_iterator search_start(str.cbegin());

        while (std::regex_search(search_start, str.cend(), match, pattern)) {
            result.push_back(match[1].str());
            search_start = match.suffix().first;
        }

        return result;
    }

    bool L1Action::hasPlaceholdersInString(const std::string& str) const {
        static const std::regex pattern(R"(\$\{([^}]+)\})");
        return std::regex_search(str, pattern);
    }

    // ============================================================
    // 参数验证
    // ============================================================

    bool L1Action::validateArgValue(const std::string& arg_name, const nlohmann::json& value) const {
        const ArgDef* arg = getArgByName(arg_name);
        if (!arg) {
            return false;
        }
        return arg->validate(value);
    }

    bool L1Action::validateArgValue(int index, const nlohmann::json& value) const {
        const ArgDef* arg = getArgByIndex(index);
        if (!arg) {
            return false;
        }
        return arg->validate(value);
    }

    bool L1Action::validateArgValues(const std::vector<nlohmann::json>& values) const {
        if (args_.size() != values.size()) {
            return false;
        }

        for (size_t i = 0; i < args_.size(); ++i) {
            if (!args_[i].validate(values[i])) {
                return false;
            }
        }

        return true;
    }

    const ArgDef* L1Action::getArgByName(const std::string& name) const {
        for (const auto& arg : args_) {
            if (arg.name == name) {
                return &arg;
            }
        }
        return nullptr;
    }

    const ArgDef* L1Action::getArgByIndex(int index) const {
        for (const auto& arg : args_) {
            if (arg.index == index) {
                return &arg;
            }
        }
        return nullptr;
    }

    // ============================================================
    // 类型检查
    // ============================================================

    bool L1Action::checkParamTypes(const std::vector<DataType>& types) const {
        return signature_.validateParamTypes(types);
    }

    std::vector<DataType> L1Action::getExpectedParamTypes() const {
        return signature_.params;
    }

    // ============================================================
    // 私有解析方法
    // ============================================================

    // src/l1_action.cpp

    void L1Action::parseCommonFields(const nlohmann::json& json) {
        if (json.contains("type") && json["type"].is_string()) {
            type_ = json["type"].get<std::string>();
        }

        if (json.contains("description") && json["description"].is_string()) {
            description_ = json["description"].get<std::string>();
        }

        if (json.contains("protocol") && json["protocol"].is_string()) {
            protocol_ = json["protocol"].get<std::string>();
        }

        if (json.contains("request") && json["request"].is_string()) {
            request_ = json["request"].get<std::string>();
        }

        if (json.contains("response") && json["response"].is_string()) {
            response_ = json["response"].get<std::string>();
        }

        // ✅ 只有存在 timeout_ms 字段时才设置
        if (json.contains("timeout_ms") && json["timeout_ms"].is_number()) {
            timeout_ms_ = json["timeout_ms"].get<int>();
        }
        else {
            // 根据类型设置默认值
            if (type_ == "log" || type_ == "popup" || type_ == "show_status") {
                timeout_ms_ = 0;  // UI 和日志操作不需要超时
            }
            else if (type_ == "modbus_write_verify" || type_ == "modbus_read_cache" || type_ == "modbus_read_check") {
                timeout_ms_ = 3000;  // Modbus 操作默认 3 秒
            }
            else if (type_ == "wait") {
                timeout_ms_ = 0;  // wait 使用 duration
            }
            else {
                timeout_ms_ = 3000;  // 其他类型默认 3 秒
            }
        }

        if (json.contains("timeout_expr") && json["timeout_expr"].is_string()) {
            timeout_expr_ = json["timeout_expr"].get<std::string>();
        }
    }

    void L1Action::parseArgs(const nlohmann::json& json) {
        if (!json.contains("args") || !json["args"].is_array()) {
            return;
        }

        args_.clear();
        for (const auto& arg_json : json["args"]) {
            ArgDef arg;

            if (arg_json.contains("index") && arg_json["index"].is_number()) {
                arg.index = arg_json["index"].get<int>();
            }

            if (arg_json.contains("name") && arg_json["name"].is_string()) {
                arg.name = arg_json["name"].get<std::string>();
            }

            if (arg_json.contains("type") && arg_json["type"].is_string()) {
                arg.type = stringToDataType(arg_json["type"].get<std::string>());
            }

            if (arg_json.contains("desc") && arg_json["desc"].is_string()) {
                arg.desc = arg_json["desc"].get<std::string>();
            }

            // 范围检查
            if (arg_json.contains("min")) {
                if (arg_json["min"].is_number_integer()) {
                    arg.min_val = arg_json["min"].get<int64_t>();
                }
                else if (arg_json["min"].is_number_float()) {
                    arg.min_float = arg_json["min"].get<double>();
                }
            }

            if (arg_json.contains("max")) {
                if (arg_json["max"].is_number_integer()) {
                    arg.max_val = arg_json["max"].get<int64_t>();
                }
                else if (arg_json["max"].is_number_float()) {
                    arg.max_float = arg_json["max"].get<double>();
                }
            }

            // 默认值
            if (arg_json.contains("default")) {
                if (arg_json["default"].is_string()) {
                    arg.default_value = arg_json["default"].get<std::string>();
                }
                else {
                    arg.default_value = arg_json["default"].dump();
                }
            }

            // 枚举值
            if (arg_json.contains("enum") && arg_json["enum"].is_array()) {
                for (const auto& val : arg_json["enum"]) {
                    if (val.is_string()) {
                        arg.enum_values.push_back(val.get<std::string>());
                    }
                }
            }

            args_.push_back(std::move(arg));
        }
    }

    void L1Action::parseParseConfig(const nlohmann::json& json) {
        if (!json.contains("parse") || !json["parse"].is_object()) {
            parse_.reset();
            return;
        }

        ParseConfig config;
        const auto& p = json["parse"];

        if (p.contains("start_byte") && p["start_byte"].is_number()) {
            config.start_byte = p["start_byte"].get<int>();
        }
        if (p.contains("length") && p["length"].is_number()) {
            config.length = p["length"].get<int>();
        }
        if (p.contains("endian") && p["endian"].is_string()) {
            config.endian = p["endian"].get<std::string>();
        }
        if (p.contains("type") && p["type"].is_string()) {
            config.type = p["type"].get<std::string>();
        }

        parse_ = config;
    }

    void L1Action::parseCheckConfig(const nlohmann::json& json) {
        if (!json.contains("check") || !json["check"].is_object()) {
            check_.reset();
            return;
        }

        CheckConfig config;
        const auto& c = json["check"];

        if (c.contains("condition") && c["condition"].is_string()) {
            config.condition = c["condition"].get<std::string>();
        }
        if (c.contains("value")) {
            if (c["value"].is_string()) {
                config.value = c["value"].get<std::string>();
            }
            else {
                config.value = c["value"].dump();
            }
        }

        check_ = config;
    }

    void L1Action::parseExpression(const nlohmann::json& json) {
        if (json.contains("expression") && json["expression"].is_string()) {
            expression_ = json["expression"].get<std::string>();
        }
        else {
            expression_.reset();
        }
    }

    void L1Action::parseDuration(const nlohmann::json& json) {
        if (json.contains("duration")) {
            if (json["duration"].is_string()) {
                duration_ = json["duration"].get<std::string>();
            }
            else {
                duration_ = json["duration"].dump();
            }
        }
        else {
            duration_.reset();
        }

        if (json.contains("unit") && json["unit"].is_string()) {
            unit_ = json["unit"].get<std::string>();
        }
    }

    // ============================================================
    // 调试和打印
    // ============================================================

    std::string L1Action::toString() const {
        std::ostringstream oss;
        print(oss);
        return oss.str();
    }

    // src/l1_action.cpp - print() 函数使用英文

    void L1Action::print(std::ostream& os) const {
        os << "========================================" << std::endl;
        os << "L1 Action Information" << std::endl;
        os << "========================================" << std::endl;

        os << "Filename: " << filename_ << std::endl;
        os << "Type: " << type_ << std::endl;
        os << "Description: " << description_ << std::endl;
        os << "Protocol: " << protocol_ << std::endl;
        os << "Request: " << request_ << std::endl;

        if (!response_.empty()) {
            os << "Response: " << response_ << std::endl;
        }

        if (timeout_ms_ > 0) {
            os << "  Timeout: " << timeout_ms_ << " ms" << std::endl;
        }

        /*if (!timeout_expr_.empty()) {
            os << "Timeout: " << timeout_expr_ << " (expression)" << std::endl;
        }
        else {
            os << "Timeout: " << timeout_ms_ << "ms" << std::endl;
        }*/

        os << "Signature: " << signature_.toString() << std::endl;

        if (!args_.empty()) {
            os << "Arguments:" << std::endl;
            for (const auto& arg : args_) {
                os << "  [" << arg.index << "] " << arg.name
                    << ": " << dataTypeToString(arg.type);
                if (arg.min_val.has_value()) {
                    os << " (min=" << arg.min_val.value() << ")";
                }
                if (arg.max_val.has_value()) {
                    os << " (max=" << arg.max_val.value() << ")";
                }
                if (!arg.enum_values.empty()) {
                    os << " enum=[";
                    for (size_t i = 0; i < arg.enum_values.size(); ++i) {
                        if (i > 0) os << ",";
                        os << arg.enum_values[i];
                    }
                    os << "]";
                }
                if (arg.default_value.has_value()) {
                    os << " default=" << arg.default_value.value();
                }
                os << std::endl;
            }
        }

        if (parse_.has_value()) {
            os << "Parse: start_byte=" << parse_->start_byte
                << ", length=" << parse_->length
                << ", endian=" << parse_->endian
                << ", type=" << parse_->type << std::endl;
        }

        if (check_.has_value()) {
            os << "Check: condition=" << check_->condition
                << ", value=" << check_->value << std::endl;
        }

        if (expression_.has_value()) {
            os << "Expression: " << expression_.value() << std::endl;
        }

        if (duration_.has_value()) {
            os << "Wait: duration=" << duration_.value()
                << ", unit=" << unit_ << std::endl;
        }

        auto placeholders = extractPlaceholders();
        if (!placeholders.empty()) {
            os << "Placeholders: ";
            for (const auto& p : placeholders) {
                os << "${" << p << "} ";
            }
            os << std::endl;
        }

        os << "========================================" << std::endl;
    }

    // ============================================================
    // 流输出操作符
    // ============================================================

    std::ostream& operator<<(std::ostream& os, const L1Action& action) {
        action.print(os);
        return os;
    }

} // namespace industrial_config_engine