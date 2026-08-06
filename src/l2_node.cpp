// src/l2_node.cpp
#include "industrial_config_engine/l2_node.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <regex>

namespace industrial_config_engine {

    // ============================================================
    // JudgeConfig 实现
    // ============================================================

    bool JudgeConfig::isValid() const {
        if (type == JudgeType::NONE) {
            return true;
        }

        switch (type) {
        case JudgeType::COMPARE:
            return !condition.empty() && !value.empty();
        case JudgeType::EXPRESSION:
            return !expression.empty();
        case JudgeType::EXISTS:
            return true;
        default:
            return false;
        }
    }

    std::vector<std::string> JudgeConfig::getPlaceholders() const {
        std::vector<std::string> result;
        static const std::regex pattern(R"(\$\{([^}]+)\})");
        std::smatch match;

        auto extract = [&](const std::string& str) {
            std::string::const_iterator search_start(str.cbegin());
            while (std::regex_search(search_start, str.cend(), match, pattern)) {
                result.push_back(match[1].str());
                search_start = match.suffix().first;
            }
            };

        extract(condition);
        extract(value);
        extract(expression);

        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());

        return result;
    }

    // ============================================================
    // ActionRef 实现
    // ============================================================

    bool ActionRef::resolve() {
        if (is_inline) {
            // inline action 不需要解析
            resolved = true;
            signature.name = "inline";
            signature.return_type = DataType::VOIDDataType;
            return true;
        }

        if (template_path.empty()) {
            return false;
        }

        // 从路径中提取文件名
        size_t pos = template_path.find_last_of("/\\");
        std::string basename = (pos != std::string::npos) ? template_path.substr(pos + 1) : template_path;

        // 去除扩展名
        size_t dot_pos = basename.find_last_of('.');
        if (dot_pos != std::string::npos) {
            basename = basename.substr(0, dot_pos);
        }

        // 解析签名
        signature = L1Action::parseSignatureFromFilename(basename);
        resolved = !signature.name.empty();

        return resolved;
    }

    // ============================================================
    // NodeSignature 实现
    // ============================================================

    std::string NodeSignature::toString() const {
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

    bool NodeSignature::validateParamCount(size_t count) const {
        return params.size() == count;
    }

    // ============================================================
    // L2Node 构造和加载
    // ============================================================

    L2Node::L2Node(const nlohmann::json& json) {
        loadFromJson(json);
    }

    L2Node::L2Node(const std::string& filename, const nlohmann::json& json)
        : filename_(filename) {
        signature_ = parseSignatureFromFilename(filename);
        loadFromJson(json);
    }

    L2Node::L2Node(const std::string& filename, const std::string& json_str)
        : filename_(filename) {
        signature_ = parseSignatureFromFilename(filename);
        loadFromJsonString(json_str);
    }

    bool L2Node::loadFromJson(const nlohmann::json& json) {
        try {
            parseCommonFields(json);
            parseParams(json);
            parseAction(json);
            parseJudge(json);
            parseBranches(json);

            // 解析 Action 签名
            action_.resolve();

            // 如果签名未设置但有filename，重新解析
            if (signature_.name.empty() && !filename_.empty()) {
                signature_ = parseSignatureFromFilename(filename_);
            }

            initialized_ = true;
            return validate();
        }
        catch (const std::exception& e) {
            initialized_ = false;
            return false;
        }
    }

    bool L2Node::loadFromJsonString(const std::string& json_str) {
        try {
            nlohmann::json json = nlohmann::json::parse(json_str);
            return loadFromJson(json);
        }
        catch (const std::exception& e) {
            initialized_ = false;
            return false;
        }
    }

    bool L2Node::loadFromFile(const std::string& filepath) {
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

    nlohmann::json L2Node::toJson() const {
        nlohmann::json json;

        json["type"] = "node";
        json["name"] = name_;

        if (!description_.empty()) {
            json["description"] = description_;
        }

        if (!params_.empty()) {
            json["params"] = params_;
        }

        json["timeout_ms"] = timeout_ms_;
        json["max_retries"] = max_retries_;
        json["retry_interval"] = retry_interval_;

        // 处理 action
        nlohmann::json action_json;
        if (action_.is_inline) {
            // inline action：解析 JSON 字符串
            try {
                action_json = nlohmann::json::parse(action_.inline_json);
            }
            catch (...) {
                action_json["type"] = "inline";
            }
        }
        else {
            action_json["template"] = action_.template_path;
        }
        json["action"] = action_json;

        if (judge_.has_value() && judge_->type != JudgeType::NONE) {
            nlohmann::json judge_json;
            switch (judge_->type) {
            case JudgeType::COMPARE:
                judge_json["type"] = "compare";
                judge_json["condition"] = judge_->condition;
                judge_json["value"] = judge_->value;
                break;
            case JudgeType::EXPRESSION:
                judge_json["type"] = "expression";
                judge_json["expression"] = judge_->expression;
                break;
            case JudgeType::EXISTS:
                judge_json["type"] = "exists";
                break;
            default:
                break;
            }
            json["judge"] = judge_json;
        }

        if (!on_success_.empty()) {
            json["on_success"] = on_success_;
        }

        if (!on_failure_.empty()) {
            json["on_failure"] = on_failure_;
        }

        if (!on_timeout_.empty()) {
            json["on_timeout"] = on_timeout_;
        }

        return json;
    }

    std::string L2Node::toJsonString(bool pretty) const {
        nlohmann::json json = toJson();
        return pretty ? json.dump(4) : json.dump();
    }

    // ============================================================
    // 文件名解析（静态方法）
    // ============================================================

    NodeSignature L2Node::parseSignatureFromFilename(const std::string& filename) {
        NodeSignature sig;
        sig.return_type = DataType::VOIDDataType;
        sig.result_key = "";

        if (filename.empty()) {
            return sig;
        }

        // 与 L1 使用相同的解析逻辑
        size_t r_pos = filename.find(".r_");

        if (r_pos != std::string::npos) {
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

    bool L2Node::isValidFilename(const std::string& filename) {
        if (filename.empty()) return false;

        size_t dot_pos = filename.find('.');
        if (dot_pos == std::string::npos || dot_pos == 0) {
            return false;
        }

        // 与 L1 使用相同的验证逻辑
        std::string params_part = filename.substr(dot_pos + 1);
        size_t r_pos = params_part.find(".r_");
        if (r_pos != std::string::npos) {
            params_part = params_part.substr(0, r_pos);
        }

        if (!params_part.empty()) {
            auto types = parseTypeList(params_part);
            for (auto type : types) {
                if (type == DataType::VOIDDataType) {
                    std::string type_str = params_part.substr(0, params_part.find('_'));
                    if (!isValidDataType(type_str)) {
                        return false;
                    }
                }
            }
        }

        return true;
    }

    std::string L2Node::generateFilename(const std::string& name,
        const std::vector<DataType>& params,
        DataType return_type,
        const std::string& result_key) {
        // 与 L1 使用相同的生成逻辑
        std::string filename = name;

        if (!params.empty()) {
            filename += ".";
            filename += typeListToString(params);
        }

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

    std::vector<std::string> L2Node::extractPlaceholders() const {
        std::vector<std::string> result;

        auto addPlaceholders = [&](const std::string& str) {
            auto extracted = extractPlaceholdersFromString(str);
            result.insert(result.end(), extracted.begin(), extracted.end());
            };

        // 从参数中提取
        for (const auto& param : params_) {
            if (param.is_string()) {
                addPlaceholders(param.get<std::string>());
            }
        }

        // 从 Action 模板中提取
        addPlaceholders(action_.template_path);

        // 从 Judge 中提取
        if (judge_.has_value()) {
            for (const auto& p : judge_->getPlaceholders()) {
                result.push_back(p);
            }
        }

        // 从分支中提取
        auto extractFromBranch = [&](const nlohmann::json& branch) {
            if (branch.is_object()) {
                for (auto it = branch.begin(); it != branch.end(); ++it) {
                    if (it.value().is_string()) {
                        addPlaceholders(it.value().get<std::string>());
                    }
                }
            }
            };

        for (const auto& action : on_success_) {
            extractFromBranch(action);
        }

        for (const auto& action : on_failure_) {
            extractFromBranch(action);
        }

        // ✅ 新增：从 on_timeout 中提取
        for (const auto& action : on_timeout_) {
            extractFromBranch(action);
        }

        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());

        return result;
    }

    std::string L2Node::replacePlaceholders(const std::string& template_str,
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

    std::vector<std::string> L2Node::extractPlaceholdersFromString(const std::string& str) const {
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

    // ============================================================
    // 验证
    // ============================================================

    bool L2Node::validate() const {
        if (!validateActionRef()) return false;
        if (!validateJudge()) return false;
        if (!validateBranches()) return false;

        // 验证超时
        if (timeout_ms_ < 1) return false;

        // 验证重试间隔
        if (retry_interval_ > 0 && (retry_interval_ < 10 || retry_interval_ > 10000)) {
            return false;
        }

        return true;
    }

    bool L2Node::validateActionRef() const {
        if (action_.template_path.empty()) {
            return false;
        }
        return action_.resolved;
    }

    bool L2Node::validateJudge() const {
        if (!judge_.has_value()) {
            return true;
        }
        return judge_->isValid();
    }

    bool L2Node::validateBranches() const {
        // on_success、on_failure、on_timeout 可以为空，但不能是 null
        return true;
    }

    // ============================================================
    // 私有解析方法
    // ============================================================

    void L2Node::parseCommonFields(const nlohmann::json& json) {
        if (json.contains("name") && json["name"].is_string()) {
            name_ = json["name"].get<std::string>();
        }

        if (json.contains("description") && json["description"].is_string()) {
            description_ = json["description"].get<std::string>();
        }

        if (json.contains("timeout_ms") && json["timeout_ms"].is_number()) {
            timeout_ms_ = json["timeout_ms"].get<uint32_t>();
        }

        if (json.contains("max_retries") && json["max_retries"].is_number()) {
            max_retries_ = json["max_retries"].get<uint8_t>();
        }

        if (json.contains("retry_interval") && json["retry_interval"].is_number()) {
            retry_interval_ = json["retry_interval"].get<uint16_t>();
        }
    }

    void L2Node::parseParams(const nlohmann::json& json) {
        if (!json.contains("params")) {
            params_.clear();
            return;
        }

        if (json["params"].is_array()) {
            params_ = json["params"].get<std::vector<nlohmann::json>>();
        }
        else if (json["params"].is_object()) {
            // 支持对象格式（命名参数）
            params_.clear();
            for (auto it = json["params"].begin(); it != json["params"].end(); ++it) {
                params_.push_back({ {it.key(), it.value()} });
            }
        }
        else {
            params_.clear();
        }
    }

    void L2Node::parseAction(const nlohmann::json& json) {
        if (!json.contains("action") || !json["action"].is_object()) {
            return;
        }

        const auto& action_json = json["action"];

        if (action_json.contains("template") && action_json["template"].is_string()) {
            // template 引用
            action_.template_path = action_json["template"].get<std::string>();
            action_.is_inline = false;
            action_.inline_json.clear();
            action_.resolve();
        }
        else if (action_json.contains("type") && action_json["type"].is_string()) {
            // inline action：保存整个 JSON 对象
            action_.is_inline = true;
            action_.inline_json = action_json.dump();
            action_.template_path.clear();
            action_.signature.name = "inline";
            action_.signature.return_type = DataType::VOIDDataType;
            action_.resolved = true;
        }
    }

    void L2Node::parseJudge(const nlohmann::json& json) {
        if (!json.contains("judge") || !json["judge"].is_object()) {
            judge_.reset();
            return;
        }

        const auto& judge_json = json["judge"];
        JudgeConfig config;

        if (judge_json.contains("type") && judge_json["type"].is_string()) {
            std::string type_str = judge_json["type"].get<std::string>();
            if (type_str == "compare") {
                config.type = JudgeType::COMPARE;
                if (judge_json.contains("condition") && judge_json["condition"].is_string()) {
                    config.condition = judge_json["condition"].get<std::string>();
                }
                if (judge_json.contains("value")) {
                    if (judge_json["value"].is_string()) {
                        config.value = judge_json["value"].get<std::string>();
                    }
                    else {
                        config.value = judge_json["value"].dump();
                    }
                }
            }
            else if (type_str == "expression") {
                config.type = JudgeType::EXPRESSION;
                if (judge_json.contains("expression") && judge_json["expression"].is_string()) {
                    config.expression = judge_json["expression"].get<std::string>();
                }
            }
            else if (type_str == "exists") {
                config.type = JudgeType::EXISTS;
            }
            else {
                config.type = JudgeType::NONE;
            }
        }

        if (config.isValid()) {
            judge_ = config;
        }
        else {
            judge_.reset();
        }
    }

    void L2Node::parseBranches(const nlohmann::json& json) {
        if (json.contains("on_success") && json["on_success"].is_array()) {
            on_success_ = json["on_success"].get<std::vector<nlohmann::json>>();
        }
        else {
            on_success_.clear();
        }

        if (json.contains("on_failure") && json["on_failure"].is_array()) {
            on_failure_ = json["on_failure"].get<std::vector<nlohmann::json>>();
        }
        else {
            on_failure_.clear();
        }

        // 解析 on_timeout
        if (json.contains("on_timeout") && json["on_timeout"].is_array()) {
            on_timeout_ = json["on_timeout"].get<std::vector<nlohmann::json>>();
        }
        else {
            on_timeout_.clear();
        }
    }

    // ============================================================
    // 调试和打印
    // ============================================================

    std::string L2Node::toString() const {
        std::ostringstream oss;
        print(oss);
        return oss.str();
    }

    void L2Node::print(std::ostream& os) const {
        os << "========================================" << std::endl;
        os << "L2 Node Information" << std::endl;
        os << "========================================" << std::endl;

        os << "Filename: " << filename_ << std::endl;
        os << "Name: " << name_ << std::endl;

        if (!description_.empty()) {
            os << "Description: " << description_ << std::endl;
        }

        os << "Signature: " << signature_.toString() << std::endl;

        // 参数
        if (!params_.empty()) {
            os << "Params: ";
            for (const auto& param : params_) {
                os << param.dump() << " ";
            }
            os << std::endl;
        }

        os << "Timeout: " << timeout_ms_ << "ms" << std::endl;
        os << "Max Retries: " << (int)max_retries_ << std::endl;
        os << "Retry Interval: " << retry_interval_ << "ms" << std::endl;

        // Action
        os << "Action Template: " << action_.template_path << std::endl;
        if (action_.resolved) {
            os << "  Resolved Signature: " << action_.signature.toString() << std::endl;
        }
        else {
            os << "  [NOT RESOLVED]" << std::endl;
        }

        // Judge
        if (judge_.has_value() && judge_->type != JudgeType::NONE) {
            os << "Judge: ";
            switch (judge_->type) {
            case JudgeType::COMPARE:
                os << "compare(" << judge_->condition << ", " << judge_->value << ")";
                break;
            case JudgeType::EXPRESSION:
                os << "expression(" << judge_->expression << ")";
                break;
            case JudgeType::EXISTS:
                os << "exists()";
                break;
            default:
                break;
            }
            os << std::endl;
        }
        else {
            os << "Judge: (none - default success)" << std::endl;
        }

        // Branches
        os << "On Success: " << on_success_.size() << " actions" << std::endl;
        os << "On Failure: " << on_failure_.size() << " actions" << std::endl;
        os << "On Timeout: " << on_timeout_.size() << " actions" << std::endl;  // ✅ 新增

        // 占位符
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

    std::ostream& operator<<(std::ostream& os, const L2Node& node) {
        node.print(os);
        return os;
    }

} // namespace industrial_config_engine