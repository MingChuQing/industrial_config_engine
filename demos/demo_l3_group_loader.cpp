#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <iomanip>
#include <memory>
#include <filesystem>
#include <unordered_map>
#include <regex>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

#include "industrial_config_engine/l1_action.hpp"
#include "industrial_config_engine/l2_node.hpp"
#include "industrial_config_engine/l3_group.hpp"
#include "industrial_config_engine/types.hpp"

using namespace std;
using namespace industrial_config_engine;
using json = nlohmann::json;
namespace fs = std::filesystem;

// ============================================================
// 显示级别枚举
// ============================================================
enum class DisplayLevel {
    GROUP,      // 只显示 Group 级别
    NODE,       // 显示 Group + Node
    ACTION      // 显示所有（Group + Node + Action）
};

// ============================================================
// 全局配置
// ============================================================
struct DisplayConfig {
    DisplayLevel level = DisplayLevel::NODE;  // 默认显示到 Node 级别
    bool fill_params = false;                  // 默认不填充参数（显示原始内容）
    unordered_map<string, string> param_values; // 参数值映射
};

static DisplayConfig g_config;

// ============================================================
// L1 Action 缓存管理器
// ============================================================
class L1ActionCache {
public:
    bool loadActionFile(const string& path) {
        try {
            if (!fs::exists(path)) {
                cerr << "  ⚠ File not found: " << path << endl;
                return false;
            }

            json root = loadJsonFile(path);

            if (root.contains("type") && root["type"].get<string>() == "action_bundle") {
                json actions = root["actions"];
                int loaded_count = 0;
                for (auto& action_json : actions) {
                    if (action_json.contains("filename")) {
                        string filename = action_json["filename"].get<string>();
                        string cache_key = path + "/" + filename;
                        L1Action action(filename, action_json);
                        if (action.isInitialized()) {
                            cache_[cache_key] = action;
                            cache_by_filename_[filename] = cache_key;

                            string basename = filename;
                            size_t dot_pos = basename.find_last_of('.');
                            if (dot_pos != string::npos) {
                                basename = basename.substr(0, dot_pos);
                            }
                            cache_by_basename_[basename] = cache_key;
                            loaded_count++;
                        }
                    }
                }
                cout << "  ✅ Loaded " << loaded_count << " actions from bundle: " << path << endl;
                return true;
            }
            else {
                string filename = fs::path(path).stem().string();
                L1Action action(filename, root);
                if (action.isInitialized()) {
                    cache_[path] = action;
                    cache_by_filename_[filename] = path;

                    string basename = filename;
                    size_t dot_pos = basename.find_last_of('.');
                    if (dot_pos != string::npos) {
                        basename = basename.substr(0, dot_pos);
                    }
                    cache_by_basename_[basename] = path;
                }
                cout << "  ✅ Loaded single action: " << filename << endl;
                return true;
            }
        }
        catch (const exception& e) {
            cerr << "  ⚠ Warning: Failed to load L1 action file: " << path << " - " << e.what() << endl;
            return false;
        }
    }

    bool loadActionsFromDirectory(const string& dir_path) {
        cout << "📂 Scanning L1 action directory: " << dir_path << endl;

        if (!fs::exists(dir_path)) {
            cerr << "  ⚠ Directory not found: " << dir_path << endl;
            return false;
        }

        if (!fs::is_directory(dir_path)) {
            cerr << "  ⚠ Not a directory: " << dir_path << endl;
            return false;
        }

        try {
            for (const auto& entry : fs::recursive_directory_iterator(dir_path)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    loadActionFile(entry.path().string());
                }
            }
            cout << "  📊 Total actions cached: " << cache_.size() << endl;
            return true;
        }
        catch (const exception& e) {
            cerr << "  ⚠ Warning: Failed to scan directory: " << dir_path << " - " << e.what() << endl;
            return false;
        }
    }

    const L1Action* getAction(const string& ref) const {
        auto it = cache_.find(ref);
        if (it != cache_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    const L1Action* getActionByFilename(const string& filename) const {
        auto it = cache_by_filename_.find(filename);
        if (it != cache_by_filename_.end()) {
            return getAction(it->second);
        }
        return nullptr;
    }

    const L1Action* getActionByBasename(const string& basename) const {
        auto it = cache_by_basename_.find(basename);
        if (it != cache_by_basename_.end()) {
            return getAction(it->second);
        }
        return nullptr;
    }

    vector<string> getCacheKeys() const {
        vector<string> keys;
        for (const auto& [key, _] : cache_) {
            keys.push_back(key);
        }
        return keys;
    }

    void printStats() const {
        cout << endl;
        cout << "📊 L1 Action Cache Stats:" << endl;
        cout << "  Total cached: " << cache_.size() << endl;
        cout << "  Indexed by filename: " << cache_by_filename_.size() << endl;
        cout << "  Indexed by basename: " << cache_by_basename_.size() << endl;
        cout << endl;
    }

private:
    json loadJsonFile(const string& path) {
        ifstream file(path);
        if (!file.is_open()) {
            throw runtime_error("Cannot open file: " + path);
        }
        json root;
        file >> root;
        return root;
    }

    map<string, L1Action> cache_;
    map<string, string> cache_by_filename_;
    map<string, string> cache_by_basename_;
};

// ============================================================
// L2 Node 缓存管理器
// ============================================================
class L2NodeCache {
public:
    bool loadNodeFile(const string& path) {
        try {
            if (!fs::exists(path)) {
                cerr << "  ⚠ File not found: " << path << endl;
                return false;
            }

            json root = loadJsonFile(path);

            if (root.contains("type") && root["type"].get<string>() == "node_bundle") {
                json nodes = root["nodes"];
                int loaded_count = 0;
                for (auto& node_json : nodes) {
                    if (node_json.contains("filename")) {
                        string filename = node_json["filename"].get<string>();
                        string cache_key = path + "/" + filename;
                        L2Node node(filename, node_json);
                        if (node.isInitialized()) {
                            cache_[cache_key] = node;
                            cache_by_filename_[filename] = cache_key;
                            loaded_count++;
                        }
                    }
                }
                cout << "  ✅ Loaded " << loaded_count << " nodes from bundle: " << path << endl;
                return true;
            }
            else {
                string filename = fs::path(path).stem().string();
                L2Node node(filename, root);
                if (node.isInitialized()) {
                    cache_[path] = node;
                    cache_by_filename_[filename] = path;
                }
                cout << "  ✅ Loaded single node: " << filename << endl;
                return true;
            }
        }
        catch (const exception& e) {
            cerr << "  ⚠ Warning: Failed to load L2 node file: " << path << " - " << e.what() << endl;
            return false;
        }
    }

    bool loadNodesFromDirectory(const string& dir_path) {
        cout << "📂 Scanning L2 node directory: " << dir_path << endl;

        if (!fs::exists(dir_path)) {
            cerr << "  ⚠ Directory not found: " << dir_path << endl;
            return false;
        }

        if (!fs::is_directory(dir_path)) {
            cerr << "  ⚠ Not a directory: " << dir_path << endl;
            return false;
        }

        try {
            for (const auto& entry : fs::recursive_directory_iterator(dir_path)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    loadNodeFile(entry.path().string());
                }
            }
            cout << "  📊 Total nodes cached: " << cache_.size() << endl;
            return true;
        }
        catch (const exception& e) {
            cerr << "  ⚠ Warning: Failed to scan directory: " << dir_path << " - " << e.what() << endl;
            return false;
        }
    }

    const L2Node* getNodeByFilename(const string& filename) const {
        auto it = cache_by_filename_.find(filename);
        if (it != cache_by_filename_.end()) {
            return &cache_.at(it->second);
        }
        return nullptr;
    }

    vector<string> getCacheKeys() const {
        vector<string> keys;
        for (const auto& [key, _] : cache_) {
            keys.push_back(key);
        }
        return keys;
    }

    void printStats() const {
        cout << endl;
        cout << "📊 L2 Node Cache Stats:" << endl;
        cout << "  Total cached: " << cache_.size() << endl;
        cout << "  Indexed by filename: " << cache_by_filename_.size() << endl;
        cout << endl;
    }

private:
    json loadJsonFile(const string& path) {
        ifstream file(path);
        if (!file.is_open()) {
            throw runtime_error("Cannot open file: " + path);
        }
        json root;
        file >> root;
        return root;
    }

    map<string, L2Node> cache_;
    map<string, string> cache_by_filename_;
};

// ============================================================
// 格式化输出工具
// ============================================================
class OutputFormatter {
public:
    static void printSeparator(char ch = '=', int width = 80) {
        cout << string(width, ch) << endl;
    }

    static void printHeader(const string& text) {
        printSeparator('=');
        cout << "  " << text << endl;
        printSeparator('=');
    }

    static void printSubheader(const string& text) {
        printSeparator('-');
        cout << "  " << text << endl;
        printSeparator('-');
    }

    static string modeToString(GroupMode mode) {
        switch (mode) {
        case GroupMode::SEQUENCE: return "sequence";
        case GroupMode::PARALLEL: return "parallel";
        case GroupMode::LOOP: return "loop";
        case GroupMode::IF: return "if";
        case GroupMode::SWITCH: return "switch";
        default: return "sequence";
        }
    }

    static string loopTypeToString(LoopType type) {
        switch (type) {
        case LoopType::COUNT: return "count";
        case LoopType::WHILE: return "while";
        case LoopType::UNTIL: return "until";
        case LoopType::FOREACH: return "foreach";
        case LoopType::DOWHILE: return "dowhile";
        default: return "count";
        }
    }

    // ============================================================
    // 替换占位符
    // ============================================================
    static string replacePlaceholders(const string& str, const unordered_map<string, string>& values) {
        string result = str;
        static const regex pattern(R"(\$\{([^}]+)\})");
        smatch match;
        string::const_iterator search_start(result.cbegin());

        while (regex_search(search_start, result.cend(), match, pattern)) {
            string placeholder = match[1].str();
            auto it = values.find(placeholder);
            if (it != values.end()) {
                string replacement = it->second;
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

    // ============================================================
    // 从 JSON 中提取参数值
    // ============================================================
    static unordered_map<string, string> extractParams(const json& item) {
        unordered_map<string, string> result;

        // 从 params 中提取
        if (item.contains("params") && item["params"].is_object()) {
            for (auto& [key, value] : item["params"].items()) {
                if (value.is_string()) {
                    result[key] = value.get<string>();
                }
                else {
                    result[key] = value.dump();
                }
            }
        }

        // 从 args 中提取（Group 级别）
        if (item.contains("args") && item["args"].is_array()) {
            for (const auto& arg : item["args"]) {
                if (arg.contains("name") && arg.contains("default")) {
                    string name = arg["name"].get<string>();
                    if (arg["default"].is_string()) {
                        result[name] = arg["default"].get<string>();
                    }
                    else {
                        result[name] = arg["default"].dump();
                    }
                }
            }
        }

        return result;
    }

    // ============================================================
    // 打印 Judge
    // ============================================================
    static void printJudge(const json& judge, int indent) {
        string indent_str(indent, ' ');

        if (!judge.is_object()) return;

        cout << indent_str << "│  Judge: ";
        if (judge.contains("type") && judge["type"].is_string()) {
            string judge_type = judge["type"].get<string>();
            cout << judge_type;

            if (judge_type == "compare") {
                cout << "(";
                if (judge.contains("condition")) {
                    cout << judge["condition"].get<string>();
                }
                cout << ", ";
                if (judge.contains("value")) {
                    if (judge["value"].is_string()) {
                        cout << judge["value"].get<string>();
                    }
                    else {
                        cout << judge["value"].dump();
                    }
                }
                cout << ")";
            }
            else if (judge_type == "expression") {
                cout << "(";
                if (judge.contains("expression")) {
                    string expr = judge["expression"].get<string>();
                    if (g_config.fill_params) {
                        expr = replacePlaceholders(expr, g_config.param_values);
                    }
                    cout << expr;
                }
                cout << ")";
            }
            else if (judge_type == "exists") {
                cout << "()";
            }
            else if (judge_type == "choice") {
                cout << "(";
                if (judge.contains("value")) {
                    cout << judge["value"].dump();
                }
                cout << ")";
            }
        }
        cout << endl;
    }

    // ============================================================
    // 打印 Condition
    // ============================================================
    static void printCondition(const json& cond, int indent) {
        string indent_str(indent, ' ');

        if (!cond.is_object()) return;

        if (cond.contains("type") && cond["type"].is_string()) {
            cout << indent_str << "Type: " << cond["type"].get<string>() << endl;
        }
        if (cond.contains("condition") && cond["condition"].is_string()) {
            cout << indent_str << "Condition: " << cond["condition"].get<string>() << endl;
        }
        if (cond.contains("value")) {
            string val = cond["value"].dump();
            if (g_config.fill_params) {
                val = replacePlaceholders(val, g_config.param_values);
            }
            cout << indent_str << "Value: " << val << endl;
        }
        if (cond.contains("source") && cond["source"].is_string()) {
            string src = cond["source"].get<string>();
            if (g_config.fill_params) {
                src = replacePlaceholders(src, g_config.param_values);
            }
            cout << indent_str << "Source: " << src << endl;
        }
        if (cond.contains("expression") && cond["expression"].is_string()) {
            string expr = cond["expression"].get<string>();
            if (g_config.fill_params) {
                expr = replacePlaceholders(expr, g_config.param_values);
            }
            cout << indent_str << "Expression: " << expr << endl;
        }
        if (cond.contains("variable") && cond["variable"].is_string()) {
            cout << indent_str << "Variable: " << cond["variable"].get<string>() << endl;
        }
    }

    // ============================================================
    // 打印 Branch (on_success/on_failure/on_timeout)
    // ============================================================
    static void printBranch(const string& label, const json& item, const string& branch_name, int indent) {
        // 如果显示级别是 ACTION，才展开 Action
        if (g_config.level != DisplayLevel::ACTION) {
            // 只显示数量，不展开
            if (item.contains(branch_name) && item[branch_name].is_array()) {
                const auto& branch = item[branch_name];
                if (!branch.empty()) {
                    cout << string(indent, ' ') << "│  " << label << ": " << branch.size() << " actions" << endl;
                }
            }
            return;
        }

        string indent_str(indent, ' ');
        string indent2(indent + 2, ' ');

        if (!item.contains(branch_name) || !item[branch_name].is_array()) {
            return;
        }

        const auto& branch = item[branch_name];
        if (branch.empty()) return;

        cout << indent_str << "│  " << label << " (" << branch.size() << " actions):" << endl;

        for (size_t i = 0; i < branch.size(); i++) {
            const auto& action = branch[i];
            if (!action.is_object()) continue;

            string prefix = (i == branch.size() - 1) ? "└─ " : "├─ ";
            cout << indent2 << prefix;

            if (action.contains("template")) {
                cout << "template: " << action["template"].get<string>();

                if (action.contains("params") && action["params"].is_array()) {
                    cout << " (params: ";
                    for (size_t j = 0; j < action["params"].size(); j++) {
                        if (j > 0) cout << ", ";
                        string val = action["params"][j].dump();
                        if (g_config.fill_params) {
                            val = replacePlaceholders(val, g_config.param_values);
                        }
                        cout << val;
                    }
                    cout << ")";
                }
            }
            else if (action.contains("type")) {
                cout << "type: " << action["type"].get<string>();

                if (action.contains("message")) {
                    string msg = action["message"].get<string>();
                    if (g_config.fill_params) {
                        msg = replacePlaceholders(msg, g_config.param_values);
                    }
                    cout << ", message: " << msg;
                }
                if (action.contains("level")) {
                    cout << ", level: " << action["level"].get<string>();
                }
                if (action.contains("params") && action["params"].is_array()) {
                    cout << ", params: [";
                    for (size_t j = 0; j < action["params"].size(); j++) {
                        if (j > 0) cout << ", ";
                        string val = action["params"][j].dump();
                        if (g_config.fill_params) {
                            val = replacePlaceholders(val, g_config.param_values);
                        }
                        cout << val;
                    }
                    cout << "]";
                }
                if (action.contains("action")) {
                    cout << ", ui_action: " << action["action"].get<string>();
                }
                if (action.contains("data") && action["data"].is_object()) {
                    cout << ", data: {";
                    bool first = true;
                    for (auto& [key, value] : action["data"].items()) {
                        if (!first) cout << ", ";
                        string val = value.dump();
                        if (g_config.fill_params) {
                            val = replacePlaceholders(val, g_config.param_values);
                        }
                        cout << key << ": " << val;
                        first = false;
                    }
                    cout << "}";
                }
            }
            cout << endl;
        }
    }

    // ============================================================
    // 打印 Body Item (递归)
    // ============================================================
    static void printBodyItem(const json& item, int indent = 4, int index = -1) {
        string indent_str(indent, ' ');

        if (index >= 0) {
            cout << indent_str << "[" << index << "] ";
        }

        if (!item.is_object() || !item.contains("type")) {
            cout << "⚠ Invalid item" << endl;
            return;
        }

        string type = item["type"].get<string>();

        // ============================================================
        // L2 Node
        // ============================================================
        if (type == "node") {
            // 如果级别是 GROUP，不显示 Node
            if (g_config.level == DisplayLevel::GROUP) {
                return;
            }

            string name = item.value("name", "unnamed");
            string result_key = item.value("result_key", "");
            string description = item.value("description", "");

            cout << "┌─ NODE: " << name;
            if (!result_key.empty()) {
                cout << " -> " << result_key;
            }
            cout << endl;

            if (!description.empty()) {
                cout << indent_str << "│  Description: " << description << endl;
            }

            // 收集参数用于填充
            auto params = extractParams(item);
            for (auto& [key, value] : params) {
                g_config.param_values[key] = value;
            }

            // params
            if (item.contains("params") && item["params"].is_object()) {
                cout << indent_str << "│  Params: ";
                bool first = true;
                for (auto& [key, value] : item["params"].items()) {
                    if (!first) cout << ", ";
                    string val = value.dump();
                    if (g_config.fill_params) {
                        val = replacePlaceholders(val, g_config.param_values);
                    }
                    cout << key << "=" << val;
                    first = false;
                }
                cout << endl;
            }

            // timeout
            if (item.contains("timeout_ms") && item["timeout_ms"].is_number()) {
                cout << indent_str << "│  Timeout: " << item["timeout_ms"].get<int>() << " ms" << endl;
            }

            // max_retries
            if (item.contains("max_retries") && item["max_retries"].is_number()) {
                cout << indent_str << "│  Max Retries: " << item["max_retries"].get<int>() << endl;
            }

            // retry_interval
            if (item.contains("retry_interval") && item["retry_interval"].is_number()) {
                cout << indent_str << "│  Retry Interval: " << item["retry_interval"].get<int>() << " ms" << endl;
            }

            // node template
            if (item.contains("node") && item["node"].is_object()) {
                if (item["node"].contains("template")) {
                    cout << indent_str << "│  Node Template: " << item["node"]["template"].get<string>() << endl;
                }
            }

            // judge
            if (item.contains("judge") && item["judge"].is_object()) {
                printJudge(item["judge"], indent);
            }

            // branches (on_success/on_failure/on_timeout)
            printBranch("On Success", item, "on_success", indent);
            printBranch("On Failure", item, "on_failure", indent);
            printBranch("On Timeout", item, "on_timeout", indent);

            cout << indent_str << "└─────────────────" << endl;

            return;
        }

        // ============================================================
        // L3 Group
        // ============================================================
        if (type == "group") {
            string name = item.value("name", "unnamed");
            string mode = item.value("mode", "sequence");
            string description = item.value("description", "");

            cout << "┌─ GROUP: " << name;
            cout << " [mode: " << mode << "]";
            cout << endl;

            if (!description.empty()) {
                cout << indent_str << "│  Description: " << description << endl;
            }

            // max_nodes
            if (item.contains("max_nodes") && item["max_nodes"].is_number()) {
                cout << indent_str << "│  Max Nodes: " << item["max_nodes"].get<int>() << endl;
            }

            // delay_between
            if (item.contains("delay_between") && item["delay_between"].is_number()) {
                int delay = item["delay_between"].get<int>();
                if (delay > 0) {
                    cout << indent_str << "│  Delay Between: " << delay << " ms" << endl;
                }
            }

            // confirm_between
            if (item.contains("confirm_between") && item["confirm_between"].is_boolean()) {
                if (item["confirm_between"].get<bool>()) {
                    cout << indent_str << "│  Confirm Between: true (需要用户确认)" << endl;
                }
            }

            // ============================================================
            // mode: loop
            // ============================================================
            if (mode == "loop" && item.contains("loop") && item["loop"].is_object()) {
                const auto& loop = item["loop"];
                cout << indent_str << "│  Loop:" << endl;

                if (loop.contains("type") && loop["type"].is_string()) {
                    string loop_type = loop["type"].get<string>();
                    cout << indent_str << "│    Type: " << loop_type << endl;

                    if (loop_type == "count" && loop.contains("count")) {
                        cout << indent_str << "│    Count: " << loop["count"].get<int>() << endl;
                    }
                    else if ((loop_type == "while" || loop_type == "until" || loop_type == "dowhile")
                        && loop.contains("condition")) {
                        string cond = loop["condition"].get<string>();
                        if (g_config.fill_params) {
                            cond = replacePlaceholders(cond, g_config.param_values);
                        }
                        cout << indent_str << "│    Condition: " << cond << endl;
                    }
                    else if (loop_type == "foreach") {
                        if (loop.contains("items")) {
                            string items = loop["items"].get<string>();
                            if (g_config.fill_params) {
                                items = replacePlaceholders(items, g_config.param_values);
                            }
                            cout << indent_str << "│    Items: " << items << endl;
                        }
                        if (loop.contains("item_name")) {
                            cout << indent_str << "│    Item Name: " << loop["item_name"].get<string>() << endl;
                        }
                    }
                }

                if (loop.contains("max_iterations") && loop["max_iterations"].is_number()) {
                    cout << indent_str << "│    Max Iterations: " << loop["max_iterations"].get<int>() << endl;
                }
            }

            // ============================================================
            // mode: if
            // ============================================================
            if (mode == "if" && item.contains("condition") && item["condition"].is_object()) {
                cout << indent_str << "│  Condition:" << endl;
                printCondition(item["condition"], indent + 4);
            }

            // ============================================================
            // mode: switch
            // ============================================================
            if (mode == "switch" && item.contains("condition") && item["condition"].is_object()) {
                cout << indent_str << "│  Switch Condition:" << endl;
                const auto& cond = item["condition"];
                if (cond.contains("type") && cond["type"].is_string()) {
                    cout << indent_str << "│    Type: " << cond["type"].get<string>() << endl;
                }
                if (cond.contains("source") && cond["source"].is_string()) {
                    string src = cond["source"].get<string>();
                    if (g_config.fill_params) {
                        src = replacePlaceholders(src, g_config.param_values);
                    }
                    cout << indent_str << "│    Source: " << src << endl;
                }
            }

            // ============================================================
            // Then/Else (if mode)
            // ============================================================
            if (item.contains("then") && item["then"].is_array()) {
                cout << indent_str << "│  Then (" << item["then"].size() << " items):" << endl;
                for (size_t i = 0; i < item["then"].size(); i++) {
                    printBodyItem(item["then"][i], indent + 4, i);
                }
            }

            if (item.contains("else") && item["else"].is_array()) {
                cout << indent_str << "│  Else (" << item["else"].size() << " items):" << endl;
                for (size_t i = 0; i < item["else"].size(); i++) {
                    printBodyItem(item["else"][i], indent + 4, i);
                }
            }

            // ============================================================
            // Cases (switch mode)
            // ============================================================
            if (item.contains("cases") && item["cases"].is_array()) {
                cout << indent_str << "│  Cases (" << item["cases"].size() << " branches):" << endl;
                for (size_t i = 0; i < item["cases"].size(); i++) {
                    const auto& case_item = item["cases"][i];
                    if (case_item.contains("case")) {
                        string case_val = case_item["case"].dump();
                        if (g_config.fill_params) {
                            case_val = replacePlaceholders(case_val, g_config.param_values);
                        }
                        cout << indent_str << "│    [" << i << "] case: " << case_val << endl;
                    }
                    if (case_item.contains("body") && case_item["body"].is_array()) {
                        for (size_t j = 0; j < case_item["body"].size(); j++) {
                            printBodyItem(case_item["body"][j], indent + 8, j);
                        }
                    }
                }
            }

            if (item.contains("default") && item["default"].is_array()) {
                cout << indent_str << "│  Default (" << item["default"].size() << " items):" << endl;
                for (size_t i = 0; i < item["default"].size(); i++) {
                    printBodyItem(item["default"][i], indent + 4, i);
                }
            }

            // ============================================================
            // Body (sequence / parallel)
            // ============================================================
            if (item.contains("body") && item["body"].is_array()) {
                cout << indent_str << "│  Body (" << item["body"].size() << " items):" << endl;
                for (size_t i = 0; i < item["body"].size(); i++) {
                    printBodyItem(item["body"][i], indent + 4, i);
                }
            }

            // Args
            if (item.contains("args") && item["args"].is_array()) {
                cout << indent_str << "│  Args:" << endl;
                for (const auto& arg : item["args"]) {
                    cout << indent_str << "│    ";
                    if (arg.contains("index")) {
                        cout << "[" << arg["index"].get<int>() << "] ";
                    }
                    if (arg.contains("name")) {
                        cout << arg["name"].get<string>();
                    }
                    if (arg.contains("type")) {
                        cout << ": " << arg["type"].get<string>();
                    }
                    if (arg.contains("default")) {
                        string def = arg["default"].dump();
                        if (g_config.fill_params) {
                            def = replacePlaceholders(def, g_config.param_values);
                        }
                        cout << " (default: " << def << ")";
                    }
                    if (arg.contains("desc")) {
                        cout << " - " << arg["desc"].get<string>();
                    }
                    cout << endl;
                }
            }

            cout << indent_str << "└─────────────────" << endl;

            return;
        }

        // 未知类型
        cout << "Unknown type: " << type << endl;
    }

    // ============================================================
    // 打印 Group 详情
    // ============================================================
    static void printGroupDetails(const L3Group& group) {
        cout << endl;
        printSubheader(group.getFilename());

        cout << "  Name: " << group.getName() << endl;
        if (!group.getDescription().empty()) {
            cout << "  Description: " << group.getDescription() << endl;
        }
        cout << "  Mode: " << modeToString(group.getMode()) << endl;
        cout << "  Max Nodes: " << group.getMaxNodes() << endl;
        cout << "  Delay Between: " << group.getDelayBetween() << " ms" << endl;
        cout << "  Confirm Between: " << (group.getConfirmBetween() ? "true" : "false") << endl;
        cout << "  Total Nodes Count: " << group.countNodes() << endl;

        // 从 Group 的 args 中提取默认参数
        const auto& args = group.getArgs();
        if (!args.empty()) {
            for (const auto& arg : args) {
                if (arg.default_value.has_value()) {
                    g_config.param_values[arg.name] = arg.default_value.value();
                }
            }
        }

        // Args
        if (!args.empty()) {
            cout << endl;
            cout << "  Args:" << endl;
            for (const auto& arg : args) {
                cout << "    [" << arg.index << "] " << arg.name << ": " << dataTypeToString(arg.type);
                if (arg.default_value.has_value()) {
                    string def = arg.default_value.value();
                    if (g_config.fill_params) {
                        def = replacePlaceholders(def, g_config.param_values);
                    }
                    cout << " (default: " << def << ")";
                }
                if (!arg.desc.empty()) {
                    cout << " - " << arg.desc;
                }
                cout << endl;
            }
        }

        // Loop
        if (group.hasLoop()) {
            const auto& loop = group.getLoop().value();
            cout << endl;
            cout << "  Loop:" << endl;
            cout << "    Type: " << loopTypeToString(loop.type) << endl;
            if (loop.type == LoopType::COUNT) {
                cout << "    Count: " << loop.count << endl;
            }
            else if (loop.type == LoopType::WHILE || loop.type == LoopType::UNTIL) {
                string cond = loop.condition;
                if (g_config.fill_params) {
                    cond = replacePlaceholders(cond, g_config.param_values);
                }
                cout << "    Condition: " << cond << endl;
            }
            else if (loop.type == LoopType::FOREACH) {
                string items = loop.items;
                if (g_config.fill_params) {
                    items = replacePlaceholders(items, g_config.param_values);
                }
                cout << "    Items: " << items << endl;
                cout << "    Item Name: " << loop.item_name << endl;
            }
            cout << "    Max Iterations: " << loop.max_iterations << endl;
        }

        // Condition (if)
        if (group.hasCondition()) {
            const auto& cond = group.getCondition().value();
            cout << endl;
            cout << "  Condition:" << endl;
            cout << "    Type: " << cond.type << endl;
            if (!cond.condition.empty()) {
                cout << "    Condition: " << cond.condition << endl;
            }
            if (!cond.value.empty()) {
                string val = cond.value;
                if (g_config.fill_params) {
                    val = replacePlaceholders(val, g_config.param_values);
                }
                cout << "    Value: " << val << endl;
            }
            if (!cond.source.empty()) {
                string src = cond.source;
                if (g_config.fill_params) {
                    src = replacePlaceholders(src, g_config.param_values);
                }
                cout << "    Source: " << src << endl;
            }
            if (!cond.expression.empty()) {
                string expr = cond.expression;
                if (g_config.fill_params) {
                    expr = replacePlaceholders(expr, g_config.param_values);
                }
                cout << "    Expression: " << expr << endl;
            }
        }

        // Then/Else (if)
        if (group.hasThen()) {
            const auto& then_body = group.getThen();
            cout << endl;
            cout << "  Then (" << then_body.size() << " items):" << endl;
            for (size_t i = 0; i < then_body.size(); i++) {
                printBodyItem(then_body[i], 4, i);
            }
        }

        if (group.hasElse()) {
            const auto& else_body = group.getElse();
            cout << endl;
            cout << "  Else (" << else_body.size() << " items):" << endl;
            for (size_t i = 0; i < else_body.size(); i++) {
                printBodyItem(else_body[i], 4, i);
            }
        }

        // Switch cases
        if (group.hasCases()) {
            const auto& cases = group.getCases();
            cout << endl;
            cout << "  Cases (" << cases.size() << " branches):" << endl;
            for (size_t i = 0; i < cases.size(); i++) {
                const auto& c = cases[i];
                string case_val = c.case_value;
                if (g_config.fill_params) {
                    case_val = replacePlaceholders(case_val, g_config.param_values);
                }
                cout << "    [" << i << "] case: " << case_val << endl;
                if (c.body.is_array()) {
                    cout << "        Body (" << c.body.size() << " items):" << endl;
                    for (size_t j = 0; j < c.body.size(); j++) {
                        printBodyItem(c.body[j], 8, j);
                    }
                }
            }
        }

        if (group.hasDefault()) {
            const auto& default_body = group.getDefault();
            cout << endl;
            cout << "  Default (" << default_body.size() << " items):" << endl;
            for (size_t i = 0; i < default_body.size(); i++) {
                printBodyItem(default_body[i], 4, i);
            }
        }

        // Body (sequence/parallel/loop)
        const auto& body = group.getBody();
        if (!body.is_null() && body.is_array()) {
            cout << endl;
            cout << "  Body (" << body.size() << " items):" << endl;
            for (size_t i = 0; i < body.size(); i++) {
                printBodyItem(body[i], 4, i);
            }
        }

        printSeparator('-');
    }
};

// ============================================================
// 解析命令行参数
// ============================================================
static void parseCommandLine(int argc, char* argv[], string& base_dir) {
    base_dir = "";

    for (int i = 1; i < argc; i++) {
        string arg = argv[i];

        if (arg == "--level=action") {
            g_config.level = DisplayLevel::ACTION;
            cout << "📌 Display level: ACTION (show all details)" << endl;
        }
        else if (arg == "--level=node") {
            g_config.level = DisplayLevel::NODE;
            cout << "📌 Display level: NODE (show Group + Node)" << endl;
        }
        else if (arg == "--level=group") {
            g_config.level = DisplayLevel::GROUP;
            cout << "📌 Display level: GROUP (show Group only)" << endl;
        }
        else if (arg.rfind("--fill=", 0) == 0) {
            string val = arg.substr(7);
            g_config.fill_params = (val == "true" || val == "1" || val == "yes");
            cout << "📌 Fill parameters: " << (g_config.fill_params ? "true" : "false") << endl;
        }
        else if (arg.rfind("--param=", 0) == 0) {
            // 格式: --param=key=value
            string kv = arg.substr(8);
            size_t eq_pos = kv.find('=');
            if (eq_pos != string::npos) {
                string key = kv.substr(0, eq_pos);
                string value = kv.substr(eq_pos + 1);
                g_config.param_values[key] = value;
                cout << "📌 Param override: " << key << "=" << value << endl;
            }
        }
        else if (arg[0] != '-') {
            // 目录参数
            base_dir = arg;
        }
    }

    // 如果指定了 fill=true 但没有提供参数，使用默认测试参数
    if (g_config.fill_params && g_config.param_values.empty()) {
        g_config.param_values["instance_id"] = "\"motor1\"";
        g_config.param_values["speed"] = "1000";
        g_config.param_values["acc_time"] = "500";
        g_config.param_values["mode"] = "0x0002";
        g_config.param_values["path"] = "0x0001";
        g_config.param_values["pr_mode"] = "0x0001";
        g_config.param_values["value"] = "100";
        g_config.param_values["timeout"] = "2000";
        cout << "📌 Using default test parameters for filling" << endl;
    }
}

// ============================================================
// 路径工具函数
// ============================================================

static string getExecutablePath() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return fs::path(buffer).parent_path().string();
#else
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1) {
        buffer[len] = '\0';
        return fs::path(buffer).parent_path().string();
    }
    return fs::current_path().string();
#endif
}

static string findConfigDirectory(const string& exe_dir) {
    vector<string> possible_paths = {
        exe_dir + "/examples/config_examples",
        exe_dir + "/../examples/config_examples",
        exe_dir + "/../../examples/config_examples",
        exe_dir + "/../../../examples/config_examples",
        fs::current_path().string() + "/examples/config_examples",
    };

    for (const auto& path : possible_paths) {
        if (fs::exists(path) && fs::is_directory(path)) {
            return path;
        }
    }

    // 向上查找
    fs::path current = fs::path(exe_dir);
    for (int i = 0; i < 6; i++) {
        current = current.parent_path();
        string test_path = (current / "examples" / "config_examples").string();
        if (fs::exists(test_path) && fs::is_directory(test_path)) {
            return test_path;
        }
        if (fs::exists(current / "L1_action") &&
            fs::exists(current / "L2_node") &&
            fs::exists(current / "L3_group")) {
            return current.string();
        }
    }

    return exe_dir;
}

// ============================================================
// 主函数
// ============================================================
int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    string base_dir;

    // 解析命令行参数
    parseCommandLine(argc, argv, base_dir);

    if (base_dir.empty()) {
        string exe_dir = getExecutablePath();
        base_dir = findConfigDirectory(exe_dir);
        cout << "📂 Using default directory: " << base_dir << endl;
    }
    else {
        if (fs::path(base_dir).is_relative()) {
            base_dir = fs::absolute(base_dir).string();
        }
        cout << "📂 Using user-specified directory: " << base_dir << endl;
    }

    string l1_dir = base_dir + "/L1_action";
    string l2_dir = base_dir + "/L2_node";
    string l3_dir = base_dir + "/L3_group";

    cout << endl;
    OutputFormatter::printHeader("L3 Group Loader Demo");
    cout << "  Base Directory: " << base_dir << endl;
    cout << "  L1 Action Directory: " << l1_dir << endl;
    cout << "  L2 Node Directory: " << l2_dir << endl;
    cout << "  L3 Group Directory: " << l3_dir << endl;
    cout << "  Display Level: ";
    switch (g_config.level) {
    case DisplayLevel::GROUP: cout << "GROUP (only groups)"; break;
    case DisplayLevel::NODE: cout << "NODE (groups + nodes)"; break;
    case DisplayLevel::ACTION: cout << "ACTION (all details)"; break;
    }
    cout << endl;
    cout << "  Fill Parameters: " << (g_config.fill_params ? "true" : "false") << endl;
    cout << endl;

    if (!fs::exists(base_dir)) {
        cerr << "❌ Error: Base directory does not exist: " << base_dir << endl;
        cerr << "   Please create the directory or specify correct path." << endl;
        return 1;
    }

    try {
        // ============================================================
        // Step 1: 加载所有 L1 Action 到缓存
        // ============================================================
        cout << "📂 Step 1: Loading L1 Actions..." << endl;
        L1ActionCache l1_cache;
        if (fs::exists(l1_dir) && fs::is_directory(l1_dir)) {
            l1_cache.loadActionsFromDirectory(l1_dir);
        }
        else {
            cerr << "  ⚠ L1 action directory does not exist: " << l1_dir << endl;
        }
        l1_cache.printStats();

        // ============================================================
        // Step 2: 加载所有 L2 Node 到缓存
        // ============================================================
        cout << "📂 Step 2: Loading L2 Nodes..." << endl;
        L2NodeCache l2_cache;
        if (fs::exists(l2_dir) && fs::is_directory(l2_dir)) {
            l2_cache.loadNodesFromDirectory(l2_dir);
        }
        else {
            cerr << "  ⚠ L2 node directory does not exist: " << l2_dir << endl;
        }
        l2_cache.printStats();

        // ============================================================
// Step 3: 加载所有 L3 Group（递归遍历所有子目录，加载所有 JSON）
// ============================================================
        cout << "📂 Step 3: Loading L3 Groups..." << endl;
        vector<L3Group> groups;

        if (fs::exists(l3_dir) && fs::is_directory(l3_dir)) {
            cout << "📂 Scanning L3 group directory (recursive, all JSON files): " << l3_dir << endl;
            try {
                // ✅ 递归遍历所有子目录
                for (const auto& entry : fs::recursive_directory_iterator(l3_dir)) {
                    // ✅ 所有 .json 文件都加载，不再过滤文件名
                    if (entry.is_regular_file() && entry.path().extension() == ".json") {
                        string path = entry.path().string();
                        L3Group group;
                        if (group.loadFromFile(path)) {
                            groups.push_back(group);
                            // 显示相对路径
                            string rel_path = fs::relative(path, l3_dir).string();
                            cout << "  ✅ Loaded group: " << group.getFilename()
                                << " (" << group.getName() << ")"
                                << " [" << rel_path << "]" << endl;
                        }
                        else {
                            cerr << "  ⚠ Failed to load: " << path << endl;
                        }
                    }
                }
            }
            catch (const exception& e) {
                cerr << "  ⚠ Failed to scan L3_group directory: " << e.what() << endl;
            }
        }
        else {
            cerr << "  ⚠ L3 group directory does not exist: " << l3_dir << endl;
        }

        cout << "  📊 Total groups loaded: " << groups.size() << endl;
        cout << endl;

        // ============================================================
        // Step 4: 显示每个 Group 的详细信息
        // ============================================================
        cout << endl;
        OutputFormatter::printSeparator('=');
        cout << "  L3 Groups Loaded: " << groups.size() << endl;
        OutputFormatter::printSeparator('=');

        if (!groups.empty()) {
            int index = 1;
            for (auto& group : groups) {
                cout << endl;
                cout << "[" << index++ << "/" << groups.size() << "]";
                OutputFormatter::printGroupDetails(group);
            }
        }
        else {
            cout << endl;
            cout << "  ⚠ No groups loaded." << endl;
            cout << "  💡 Please ensure L3_group directory contains valid group files." << endl;
            cout << "  💡 Expected file pattern: *.group.json or *.group.*.json" << endl;
        }

        // ============================================================
        // Step 5: 完成
        // ============================================================
        cout << endl;
        OutputFormatter::printHeader("加载完成");
        cout << "  Total L1 Actions cached: " << l1_cache.getCacheKeys().size() << endl;
        cout << "  Total L2 Nodes cached: " << l2_cache.getCacheKeys().size() << endl;
        cout << "  Total L3 Groups loaded: " << groups.size() << endl;
        cout << endl;

    }
    catch (const exception& e) {
        cerr << endl;
        cerr << "❌ Error: " << e.what() << endl;
        return 1;
    }

    return 0;
}