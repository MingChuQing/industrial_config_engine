// src/l3_group.cpp
#include "industrial_config_engine/l3_group.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <regex>
#include <filesystem>
#include <set>

namespace industrial_config_engine {

    namespace fs = std::filesystem;

    // ============================================================
    // L3Group 构造和加载
    // ============================================================

    L3Group::L3Group(const nlohmann::json& json) {
        loadFromJson(json);
    }

    L3Group::L3Group(const std::string& filename, const nlohmann::json& json)
        : filename_(filename) {
        loadFromJson(json);
    }

    L3Group::L3Group(const std::string& filename, const std::string& json_str)
        : filename_(filename) {
        loadFromJsonString(json_str);
    }

    bool L3Group::loadFromJson(const nlohmann::json& json) {
        try {
            parseCommonFields(json);
            parseArgs(json);
            parseBody(json);
            parseLoop(json);
            parseCondition(json);
            parseThenElse(json);
            parseSwitch(json);

            initialized_ = true;
            return validate();
        }
        catch (const std::exception& e) {
            initialized_ = false;
            return false;
        }
    }

    bool L3Group::loadFromJsonString(const std::string& json_str) {
        try {
            nlohmann::json json = nlohmann::json::parse(json_str);
            return loadFromJson(json);
        }
        catch (const std::exception& e) {
            initialized_ = false;
            return false;
        }
    }

    bool L3Group::loadFromFile(const std::string& filepath) {
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
        if (pos != std::string::npos) {
            // 去除 .group.json 或 .group.xxx.json
            std::string name_part = basename.substr(0, pos);
            size_t group_pos = name_part.find(".group");
            if (group_pos != std::string::npos) {
                filename_ = name_part.substr(0, group_pos);
            }
            else {
                filename_ = name_part;
            }
        }
        else {
            filename_ = basename;
        }

        return loadFromJson(json);
    }

    // ============================================================
    // 导出为 JSON
    // ============================================================

    nlohmann::json L3Group::toJson() const {
        nlohmann::json json;

        json["type"] = "group";
        json["name"] = name_;

        if (!description_.empty()) {
            json["description"] = description_;
        }

        json["mode"] = modeToString(mode_);
        json["max_nodes"] = max_nodes_;
        json["delay_between"] = delay_between_;
        json["confirm_between"] = confirm_between_;

        if (!body_.is_null()) {
            json["body"] = body_;
        }

        // 参数
        if (!args_.empty()) {
            nlohmann::json args_json = nlohmann::json::array();
            for (const auto& arg : args_) {
                nlohmann::json arg_json;
                arg_json["index"] = arg.index;
                arg_json["name"] = arg.name;
                arg_json["type"] = dataTypeToString(arg.type);
                if (arg.default_value.has_value()) {
                    arg_json["default"] = arg.default_value.value();
                }
                if (!arg.desc.empty()) {
                    arg_json["desc"] = arg.desc;
                }
                args_json.push_back(arg_json);
            }
            json["args"] = args_json;
        }

        // Loop
        if (loop_.has_value()) {
            nlohmann::json loop_json;
            loop_json["type"] = loopTypeToString(loop_->type);
            if (loop_->type == LoopType::COUNT) {
                loop_json["count"] = loop_->count;
            }
            else if (loop_->type == LoopType::WHILE || loop_->type == LoopType::UNTIL) {
                loop_json["condition"] = loop_->condition;
            }
            else if (loop_->type == LoopType::FOREACH) {
                loop_json["items"] = loop_->items;
                loop_json["item_name"] = loop_->item_name;
            }
            if (loop_->max_iterations > 0) {
                loop_json["max_iterations"] = loop_->max_iterations;
            }
            json["loop"] = loop_json;
        }

        // Condition (if)
        if (condition_.has_value()) {
            nlohmann::json cond_json;
            cond_json["type"] = condition_->type;
            if (!condition_->condition.empty()) {
                cond_json["condition"] = condition_->condition;
            }
            if (!condition_->value.empty()) {
                cond_json["value"] = condition_->value;
            }
            if (!condition_->source.empty()) {
                cond_json["source"] = condition_->source;
            }
            if (!condition_->expression.empty()) {
                cond_json["expression"] = condition_->expression;
            }
            if (!condition_->variable.empty()) {
                cond_json["variable"] = condition_->variable;
            }
            json["condition"] = cond_json;
        }

        // Then/Else (if)
        if (!then_body_.is_null()) {
            json["then"] = then_body_;
        }
        if (!else_body_.is_null()) {
            json["else"] = else_body_;
        }

        // Switch cases
        if (!cases_.empty()) {
            nlohmann::json cases_json = nlohmann::json::array();
            for (const auto& c : cases_) {
                nlohmann::json case_json;
                case_json["case"] = c.case_value;
                case_json["body"] = c.body;
                cases_json.push_back(case_json);
            }
            json["cases"] = cases_json;
        }
        if (!default_body_.is_null()) {
            json["default"] = default_body_;
        }

        return json;
    }

    std::string L3Group::toJsonString(bool pretty) const {
        nlohmann::json json = toJson();
        return pretty ? json.dump(4) : json.dump();
    }

    // ============================================================
    // 验证
    // ============================================================

    bool L3Group::validate() const {
        if (name_.empty()) return false;
        if (!validateBody()) return false;
        if (!validateLoop()) return false;
        if (!validateCondition()) return false;
        if (!validateCases()) return false;
        if (!validateMaxNodes()) return false;
        return true;
    }

    bool L3Group::validateBody() const {
        if (body_.is_null()) return true;
        if (!body_.is_array()) return false;

        // 检查 body 中的每一项
        for (const auto& item : body_) {
            if (!item.is_object()) return false;
            if (!item.contains("type")) return false;
            std::string type = item["type"].get<std::string>();
            if (type != "node" && type != "group") return false;
        }

        return true;
    }

    bool L3Group::validateLoop() const {
        if (!loop_.has_value()) return true;

        const auto& loop = loop_.value();
        switch (loop.type) {
        case LoopType::COUNT:
            return loop.count > 0 && loop.count <= 1000000;
        case LoopType::WHILE:
        case LoopType::UNTIL:
            return !loop.condition.empty();
        case LoopType::FOREACH:
            return !loop.items.empty() && !loop.item_name.empty();
        case LoopType::DOWHILE:
            return !loop.condition.empty();
        default:
            return false;
        }
    }

    bool L3Group::validateCondition() const {
        if (!condition_.has_value()) return true;

        const auto& cond = condition_.value();
        if (cond.type.empty()) return false;

        if (cond.type == "compare") {
            return !cond.condition.empty() && !cond.value.empty();
        }
        else if (cond.type == "expression") {
            return !cond.expression.empty();
        }
        else if (cond.type == "exists") {
            return !cond.variable.empty();
        }
        else if (cond.type == "choice" || cond.type == "value") {
            return !cond.value.empty() || !cond.source.empty();
        }

        return false;
    }

    bool L3Group::validateCases() const {
        if (cases_.empty()) return true;

        // 检查 case 值是否唯一
        std::set<std::string> seen;
        for (const auto& c : cases_) {
            if (seen.find(c.case_value) != seen.end()) {
                return false;  // 重复 case
            }
            seen.insert(c.case_value);
            if (!c.body.is_array()) return false;
        }

        return true;
    }

    bool L3Group::validateMaxNodes() const {
        int total = countNodes();
        return total <= max_nodes_;
    }

    bool L3Group::validateCircularReference(const std::vector<std::string>& ancestors) const {
        // 检查 body 中的引用
        if (body_.is_null()) return true;

        for (const auto& item : body_) {
            if (!item.is_object()) continue;
            if (!item.contains("type")) continue;

            std::string type = item["type"].get<std::string>();
            if (type == "group" && item.contains("template")) {
                std::string template_path = item["template"].get<std::string>();
                // 检查是否循环引用
                for (const auto& ancestor : ancestors) {
                    if (template_path.find(ancestor) != std::string::npos) {
                        return false;
                    }
                }
                // 递归检查
                std::vector<std::string> new_ancestors = ancestors;
                new_ancestors.push_back(template_path);
                // 这里需要加载子 group 并递归检查
                // 在实际实现中，需要通过 L3GroupLoader 加载
            }
        }

        return true;
    }

    // ============================================================
    // 节点计数
    // ============================================================

    int L3Group::countNodes() const {
        return countNodesInJson(body_);
    }

    int L3Group::countNodesInJson(const nlohmann::json& body) const {
        if (body.is_null() || !body.is_array()) return 0;

        int total = 0;
        for (const auto& item : body) {
            if (!item.is_object()) continue;
            if (!item.contains("type")) continue;

            std::string type = item["type"].get<std::string>();
            if (type == "node") {
                total++;
            }
            else if (type == "group") {
                if (item.contains("body") && item["body"].is_array()) {
                    // 内联 group
                    total += countNodesInJson(item["body"]);
                }
                else if (item.contains("then") && item["then"].is_array()) {
                    // if 的 then 分支
                    total += countNodesInJson(item["then"]);
                    if (item.contains("else") && item["else"].is_array()) {
                        total += countNodesInJson(item["else"]);
                    }
                }
                else if (item.contains("cases") && item["cases"].is_array()) {
                    // switch 的 cases
                    for (const auto& case_item : item["cases"]) {
                        if (case_item.contains("body") && case_item["body"].is_array()) {
                            total += countNodesInJson(case_item["body"]);
                        }
                    }
                    if (item.contains("default") && item["default"].is_array()) {
                        total += countNodesInJson(item["default"]);
                    }
                }
                else if (item.contains("template")) {
                    // 外部引用：需要加载并计数
                    // 在实际实现中，需要通过 L3GroupLoader 加载
                    // 这里假设外部引用不计数（由加载时递归计算）
                    total += 1;  // 占位
                }
            }
        }

        return total;
    }

    // ============================================================
    // 占位符处理
    // ============================================================

    std::vector<std::string> L3Group::extractPlaceholders() const {
        std::vector<std::string> result;

        auto addPlaceholders = [&](const std::string& str) {
            auto extracted = extractPlaceholdersFromString(str);
            result.insert(result.end(), extracted.begin(), extracted.end());
            };

        // 从参数中提取
        for (const auto& arg : args_) {
            addPlaceholders(arg.name);
            if (arg.default_value.has_value()) {
                addPlaceholders(arg.default_value.value());
            }
        }

        // 从 body 中提取（JSON 字符串值）
        if (!body_.is_null() && body_.is_array()) {
            std::string body_str = body_.dump();
            addPlaceholders(body_str);
        }

        // 从 loop 中提取
        if (loop_.has_value()) {
            addPlaceholders(loop_->condition);
            addPlaceholders(loop_->items);
            addPlaceholders(loop_->item_name);
        }

        // 从 condition 中提取
        if (condition_.has_value()) {
            addPlaceholders(condition_->value);
            addPlaceholders(condition_->source);
            addPlaceholders(condition_->expression);
            addPlaceholders(condition_->variable);
        }

        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());

        return result;
    }

    std::string L3Group::replacePlaceholders(const std::string& template_str,
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

    std::vector<std::string> L3Group::extractPlaceholdersFromString(const std::string& str) const {
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
    // 私有解析方法
    // ============================================================

    void L3Group::parseCommonFields(const nlohmann::json& json) {
        if (json.contains("name") && json["name"].is_string()) {
            name_ = json["name"].get<std::string>();
        }

        if (json.contains("description") && json["description"].is_string()) {
            description_ = json["description"].get<std::string>();
        }

        if (json.contains("mode") && json["mode"].is_string()) {
            mode_ = stringToMode(json["mode"].get<std::string>());
        }

        if (json.contains("max_nodes") && json["max_nodes"].is_number()) {
            max_nodes_ = json["max_nodes"].get<int>();
            if (max_nodes_ <= 0) max_nodes_ = 100;
        }

        if (json.contains("delay_between") && json["delay_between"].is_number()) {
            delay_between_ = json["delay_between"].get<int>();
        }

        if (json.contains("confirm_between") && json["confirm_between"].is_boolean()) {
            confirm_between_ = json["confirm_between"].get<bool>();
        }
    }

    void L3Group::parseArgs(const nlohmann::json& json) {
        args_.clear();

        if (!json.contains("args") || !json["args"].is_array()) {
            return;
        }

        for (const auto& arg_json : json["args"]) {
            GroupArgDef arg;
            if (arg_json.contains("index") && arg_json["index"].is_number()) {
                arg.index = arg_json["index"].get<int>();
            }
            if (arg_json.contains("name") && arg_json["name"].is_string()) {
                arg.name = arg_json["name"].get<std::string>();
            }
            if (arg_json.contains("type") && arg_json["type"].is_string()) {
                arg.type = stringToDataType(arg_json["type"].get<std::string>());
            }
            if (arg_json.contains("default")) {
                if (arg_json["default"].is_string()) {
                    arg.default_value = arg_json["default"].get<std::string>();
                }
                else if (arg_json["default"].is_number()) {
                    arg.default_value = arg_json["default"].dump();
                }
                else if (arg_json["default"].is_boolean()) {
                    arg.default_value = arg_json["default"].get<bool>() ? "true" : "false";
                }
            }
            if (arg_json.contains("desc") && arg_json["desc"].is_string()) {
                arg.desc = arg_json["desc"].get<std::string>();
            }
            args_.push_back(arg);
        }
    }

    void L3Group::parseBody(const nlohmann::json& json) {
        if (json.contains("body") && json["body"].is_array()) {
            body_ = json["body"];
        }
        else {
            body_ = nlohmann::json::array();
        }
    }

    void L3Group::parseLoop(const nlohmann::json& json) {
        loop_.reset();

        if (!json.contains("loop") || !json["loop"].is_object()) {
            return;
        }

        const auto& loop_json = json["loop"];
        LoopConfig loop;

        if (loop_json.contains("type") && loop_json["type"].is_string()) {
            loop.type = stringToLoopType(loop_json["type"].get<std::string>());
        }

        if (loop_json.contains("count") && loop_json["count"].is_number()) {
            loop.count = loop_json["count"].get<int>();
        }

        if (loop_json.contains("condition") && loop_json["condition"].is_string()) {
            loop.condition = loop_json["condition"].get<std::string>();
        }

        if (loop_json.contains("items") && loop_json["items"].is_string()) {
            loop.items = loop_json["items"].get<std::string>();
        }

        if (loop_json.contains("item_name") && loop_json["item_name"].is_string()) {
            loop.item_name = loop_json["item_name"].get<std::string>();
        }

        if (loop_json.contains("max_iterations") && loop_json["max_iterations"].is_number()) {
            loop.max_iterations = loop_json["max_iterations"].get<int>();
        }

        loop_ = loop;
    }

    void L3Group::parseCondition(const nlohmann::json& json) {
        condition_.reset();

        if (!json.contains("condition") || !json["condition"].is_object()) {
            return;
        }

        const auto& cond_json = json["condition"];
        ConditionConfig cond;

        if (cond_json.contains("type") && cond_json["type"].is_string()) {
            cond.type = cond_json["type"].get<std::string>();
        }

        if (cond_json.contains("condition") && cond_json["condition"].is_string()) {
            cond.condition = cond_json["condition"].get<std::string>();
        }

        if (cond_json.contains("value")) {
            if (cond_json["value"].is_string()) {
                cond.value = cond_json["value"].get<std::string>();
            }
            else {
                cond.value = cond_json["value"].dump();
            }
        }

        if (cond_json.contains("source") && cond_json["source"].is_string()) {
            cond.source = cond_json["source"].get<std::string>();
        }

        if (cond_json.contains("expression") && cond_json["expression"].is_string()) {
            cond.expression = cond_json["expression"].get<std::string>();
        }

        if (cond_json.contains("variable") && cond_json["variable"].is_string()) {
            cond.variable = cond_json["variable"].get<std::string>();
        }

        condition_ = cond;
    }

    void L3Group::parseThenElse(const nlohmann::json& json) {
        then_body_ = nlohmann::json();
        else_body_ = nlohmann::json();

        if (json.contains("then") && json["then"].is_array()) {
            then_body_ = json["then"];
        }

        if (json.contains("else") && json["else"].is_array()) {
            else_body_ = json["else"];
        }
    }

    void L3Group::parseSwitch(const nlohmann::json& json) {
        cases_.clear();
        default_body_ = nlohmann::json();

        if (!json.contains("cases") || !json["cases"].is_array()) {
            return;
        }

        for (const auto& case_json : json["cases"]) {
            SwitchCase sc;
            if (case_json.contains("case")) {
                if (case_json["case"].is_string()) {
                    sc.case_value = case_json["case"].get<std::string>();
                }
                else {
                    sc.case_value = case_json["case"].dump();
                }
            }
            if (case_json.contains("body") && case_json["body"].is_array()) {
                sc.body = case_json["body"];
            }
            cases_.push_back(sc);
        }

        if (json.contains("default") && json["default"].is_array()) {
            default_body_ = json["default"];
        }
    }

    // ============================================================
    // 类型转换辅助方法
    // ============================================================

    std::string L3Group::modeToString(GroupMode mode) const {
        switch (mode) {
        case GroupMode::SEQUENCE: return "sequence";
        case GroupMode::PARALLEL: return "parallel";
        case GroupMode::LOOP: return "loop";
        case GroupMode::IF: return "if";
        case GroupMode::SWITCH: return "switch";
        default: return "sequence";
        }
    }

    GroupMode L3Group::stringToMode(const std::string& mode_str) const {
        if (mode_str == "parallel") return GroupMode::PARALLEL;
        if (mode_str == "loop") return GroupMode::LOOP;
        if (mode_str == "if") return GroupMode::IF;
        if (mode_str == "switch") return GroupMode::SWITCH;
        return GroupMode::SEQUENCE;
    }

    std::string L3Group::loopTypeToString(LoopType type) const {
        switch (type) {
        case LoopType::COUNT: return "count";
        case LoopType::WHILE: return "while";
        case LoopType::UNTIL: return "until";
        case LoopType::FOREACH: return "foreach";
        case LoopType::DOWHILE: return "dowhile";
        default: return "count";
        }
    }

    LoopType L3Group::stringToLoopType(const std::string& type_str) const {
        if (type_str == "while") return LoopType::WHILE;
        if (type_str == "until") return LoopType::UNTIL;
        if (type_str == "foreach") return LoopType::FOREACH;
        if (type_str == "dowhile" || type_str == "do-while" || type_str == "do_while") return LoopType::DOWHILE;
        return LoopType::COUNT;
    }

    // ============================================================
    // 调试和打印
    // ============================================================

    std::string L3Group::toString() const {
        std::ostringstream oss;
        print(oss);
        return oss.str();
    }

    void L3Group::print(std::ostream& os) const {
        os << "========================================" << std::endl;
        os << "L3 Group Information" << std::endl;
        os << "========================================" << std::endl;

        os << "Filename: " << filename_ << std::endl;
        os << "Name: " << name_ << std::endl;

        if (!description_.empty()) {
            os << "Description: " << description_ << std::endl;
        }

        os << "Mode: " << modeToString(mode_) << std::endl;
        os << "Max Nodes: " << max_nodes_ << std::endl;
        os << "Delay Between: " << delay_between_ << "ms" << std::endl;
        os << "Confirm Between: " << (confirm_between_ ? "true" : "false") << std::endl;

        // 参数
        if (!args_.empty()) {
            os << "Args:" << std::endl;
            for (const auto& arg : args_) {
                os << "  [" << arg.index << "] " << arg.name << ": " << dataTypeToString(arg.type);
                if (arg.default_value.has_value()) {
                    os << " (default: " << arg.default_value.value() << ")";
                }
                if (!arg.desc.empty()) {
                    os << " - " << arg.desc;
                }
                os << std::endl;
            }
        }

        // Loop
        if (loop_.has_value()) {
            os << "Loop:" << std::endl;
            os << "  Type: " << loopTypeToString(loop_->type) << std::endl;
            if (loop_->type == LoopType::COUNT) {
                os << "  Count: " << loop_->count << std::endl;
            }
            else if (loop_->type == LoopType::WHILE || loop_->type == LoopType::UNTIL) {
                os << "  Condition: " << loop_->condition << std::endl;
            }
            else if (loop_->type == LoopType::FOREACH) {
                os << "  Items: " << loop_->items << std::endl;
                os << "  Item Name: " << loop_->item_name << std::endl;
            }
            os << "  Max Iterations: " << loop_->max_iterations << std::endl;
        }

        // Condition (if)
        if (condition_.has_value()) {
            os << "Condition:" << std::endl;
            os << "  Type: " << condition_->type << std::endl;
            if (!condition_->condition.empty()) {
                os << "  Condition: " << condition_->condition << std::endl;
            }
            if (!condition_->value.empty()) {
                os << "  Value: " << condition_->value << std::endl;
            }
            if (!condition_->source.empty()) {
                os << "  Source: " << condition_->source << std::endl;
            }
            if (!condition_->expression.empty()) {
                os << "  Expression: " << condition_->expression << std::endl;
            }
            if (!condition_->variable.empty()) {
                os << "  Variable: " << condition_->variable << std::endl;
            }
        }

        // Body
        if (!body_.is_null() && body_.is_array()) {
            os << "Body: " << body_.size() << " items" << std::endl;
        }

        // Then/Else (if)
        if (!then_body_.is_null() && then_body_.is_array()) {
            os << "Then: " << then_body_.size() << " items" << std::endl;
        }
        if (!else_body_.is_null() && else_body_.is_array()) {
            os << "Else: " << else_body_.size() << " items" << std::endl;
        }

        // Switch cases
        if (!cases_.empty()) {
            os << "Cases: " << cases_.size() << " branches" << std::endl;
        }
        if (!default_body_.is_null() && default_body_.is_array()) {
            os << "Default: " << default_body_.size() << " items" << std::endl;
        }

        // 节点计数
        os << "Total Nodes: " << countNodes() << std::endl;

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

    std::ostream& operator<<(std::ostream& os, const L3Group& group) {
        group.print(os);
        return os;
    }

    // ============================================================
    // L3GroupLoader 实现
    // ============================================================

    L3Group L3GroupLoader::loadFromFile(const std::string& filepath) {
        L3Group group;
        group.loadFromFile(filepath);
        return group;
    }

    std::vector<L3Group> L3GroupLoader::loadFromDirectory(const std::string& dir_path) {
        std::vector<L3Group> groups;

        try {
            for (const auto& entry : fs::recursive_directory_iterator(dir_path)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    std::string path = entry.path().string();
                    if (path.find(".group") != std::string::npos) {
                        L3Group group = loadFromFile(path);
                        if (group.isInitialized()) {
                            groups.push_back(group);
                            if (cache_) {
                                (*cache_)[path] = group;
                            }
                        }
                    }
                }
            }
        }
        catch (const std::exception& e) {
            // 忽略目录扫描错误
        }

        return groups;
    }

    std::vector<L3Group> L3GroupLoader::loadBundle(const std::string& bundle_path) {
        std::vector<L3Group> groups;
        nlohmann::json root = loadJsonFile(bundle_path);

        if (!root.contains("type") || root["type"].get<std::string>() != "group_bundle") {
            return groups;
        }

        if (!root.contains("groups") || !root["groups"].is_array()) {
            return groups;
        }

        for (const auto& group_json : root["groups"]) {
            if (group_json.contains("filename") && group_json["filename"].is_string()) {
                std::string filename = group_json["filename"].get<std::string>();
                L3Group group(filename, group_json);
                if (group.isInitialized()) {
                    groups.push_back(group);
                }
            }
        }

        return groups;
    }

    L3Group L3GroupLoader::resolveReference(const std::string& template_path,
        const std::vector<nlohmann::json>& params) {
        std::string resolved_path = resolvePath(template_path);

        // 检查缓存
        if (cache_) {
            auto it = cache_->find(resolved_path);
            if (it != cache_->end()) {
                return it->second;
            }
        }

        // 加载 Group
        L3Group group = loadFromFile(resolved_path);

        // 存入缓存
        if (cache_ && group.isInitialized()) {
            (*cache_)[resolved_path] = group;
        }

        return group;
    }

    nlohmann::json L3GroupLoader::loadJsonFile(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + path);
        }
        nlohmann::json root;
        file >> root;
        return root;
    }

    std::string L3GroupLoader::resolvePath(const std::string& template_path) {
        // 如果已经是绝对路径或相对路径，直接返回
        if (template_path.find("/") != std::string::npos ||
            template_path.find("\\") != std::string::npos) {
            return template_path;
        }

        // 尝试在 L3_group 目录下查找
        return l3_group_dir_ + "/" + template_path + ".group.json";
    }

} // namespace industrial_config_engine