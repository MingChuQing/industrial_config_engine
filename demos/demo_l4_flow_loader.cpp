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

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

#include "industrial_config_engine/l1_action.hpp"
#include "industrial_config_engine/l2_node.hpp"
#include "industrial_config_engine/l3_group.hpp"
#include "industrial_config_engine/l4_flow.hpp"
#include "industrial_config_engine/types.hpp"

using namespace std;
using namespace industrial_config_engine;
using json = nlohmann::json;
namespace fs = std::filesystem;

// ============================================================
// 显示级别枚举
// ============================================================
enum class DisplayLevel {
    FLOW,       // 只显示 Flow 级别
    GROUP,      // 显示 Flow + Group
    FULL        // 显示所有（Flow + Group + Node + Action）
};

// ============================================================
// 全局配置
// ============================================================
struct DisplayConfig {
    DisplayLevel level = DisplayLevel::GROUP;
    bool fill_params = false;
    unordered_map<string, string> param_values;
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
                cerr << "  [WARN] File not found: " << path << endl;
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
                cout << "  [OK] Loaded " << loaded_count << " actions from bundle: " << path << endl;
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
                cout << "  [OK] Loaded single action: " << filename << endl;
                return true;
            }
        }
        catch (const exception& e) {
            cerr << "  [WARN] Failed to load L1 action file: " << path << " - " << e.what() << endl;
            return false;
        }
    }

    bool loadActionsFromDirectory(const string& dir_path) {
        cout << "[DIR] Scanning L1 action directory: " << dir_path << endl;

        if (!fs::exists(dir_path)) {
            cerr << "  [WARN] Directory not found: " << dir_path << endl;
            return false;
        }

        if (!fs::is_directory(dir_path)) {
            cerr << "  [WARN] Not a directory: " << dir_path << endl;
            return false;
        }

        try {
            for (const auto& entry : fs::recursive_directory_iterator(dir_path)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    loadActionFile(entry.path().string());
                }
            }
            cout << "  [STAT] Total actions cached: " << cache_.size() << endl;
            return true;
        }
        catch (const exception& e) {
            cerr << "  [WARN] Failed to scan directory: " << dir_path << " - " << e.what() << endl;
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
        cout << "[STAT] L1 Action Cache Stats:" << endl;
        cout << "  Total cached: " << cache_.size() << endl;
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
                cerr << "  [WARN] File not found: " << path << endl;
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
                cout << "  [OK] Loaded " << loaded_count << " nodes from bundle: " << path << endl;
                return true;
            }
            else {
                string filename = fs::path(path).stem().string();
                L2Node node(filename, root);
                if (node.isInitialized()) {
                    cache_[path] = node;
                    cache_by_filename_[filename] = path;
                }
                cout << "  [OK] Loaded single node: " << filename << endl;
                return true;
            }
        }
        catch (const exception& e) {
            cerr << "  [WARN] Failed to load L2 node file: " << path << " - " << e.what() << endl;
            return false;
        }
    }

    bool loadNodesFromDirectory(const string& dir_path) {
        cout << "[DIR] Scanning L2 node directory: " << dir_path << endl;

        if (!fs::exists(dir_path)) {
            cerr << "  [WARN] Directory not found: " << dir_path << endl;
            return false;
        }

        if (!fs::is_directory(dir_path)) {
            cerr << "  [WARN] Not a directory: " << dir_path << endl;
            return false;
        }

        try {
            for (const auto& entry : fs::recursive_directory_iterator(dir_path)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    loadNodeFile(entry.path().string());
                }
            }
            cout << "  [STAT] Total nodes cached: " << cache_.size() << endl;
            return true;
        }
        catch (const exception& e) {
            cerr << "  [WARN] Failed to scan directory: " << dir_path << " - " << e.what() << endl;
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
        cout << "[STAT] L2 Node Cache Stats:" << endl;
        cout << "  Total cached: " << cache_.size() << endl;
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
// L3 Group 缓存管理器
// ============================================================
class L3GroupCache {
public:
    bool loadGroupFile(const string& path) {
        try {
            if (!fs::exists(path)) {
                cerr << "  [WARN] File not found: " << path << endl;
                return false;
            }

            L3Group group;
            if (group.loadFromFile(path)) {
                string filename = group.getFilename();
                cache_by_filename_[filename] = group;
                cache_by_path_[path] = group;
                return true;
            }
            return false;
        }
        catch (const exception& e) {
            cerr << "  [WARN] Failed to load L3 group: " << path << " - " << e.what() << endl;
            return false;
        }
    }

    bool loadGroupsFromDirectory(const string& dir_path) {
        cout << "[DIR] Scanning L3 group directory: " << dir_path << endl;

        if (!fs::exists(dir_path)) {
            cerr << "  [WARN] Directory not found: " << dir_path << endl;
            return false;
        }

        if (!fs::is_directory(dir_path)) {
            cerr << "  [WARN] Not a directory: " << dir_path << endl;
            return false;
        }

        try {
            for (const auto& entry : fs::recursive_directory_iterator(dir_path)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    string path = entry.path().string();
                    if (path.find(".group.") != string::npos ||
                        path.find(".group.json") != string::npos) {
                        loadGroupFile(path);
                    }
                }
            }
            cout << "  [STAT] Total groups cached: " << cache_by_path_.size() << endl;
            return true;
        }
        catch (const exception& e) {
            cerr << "  [WARN] Failed to scan directory: " << dir_path << " - " << e.what() << endl;
            return false;
        }
    }

    const L3Group* getGroupByFilename(const string& filename) const {
        auto it = cache_by_filename_.find(filename);
        if (it != cache_by_filename_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    const L3Group* getGroupByPath(const string& path) const {
        auto it = cache_by_path_.find(path);
        if (it != cache_by_path_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    // ============================================================
    // 新增：获取缓存 Keys
    // ============================================================
    vector<string> getCacheKeys() const {
        vector<string> keys;
        for (const auto& [key, _] : cache_by_path_) {
            keys.push_back(key);
        }
        return keys;
    }

    void printStats() const {
        cout << endl;
        cout << "[STAT] L3 Group Cache Stats:" << endl;
        cout << "  Total cached: " << cache_by_path_.size() << endl;
        cout << endl;
    }

private:
    map<string, L3Group> cache_by_filename_;
    map<string, L3Group> cache_by_path_;
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

    static void printBodyItem(const json& item, int indent = 4, int index = -1) {
        string indent_str(indent, ' ');

        if (index >= 0) {
            cout << indent_str << "[" << index << "] ";
        }

        if (!item.is_object() || !item.contains("type")) {
            cout << "[ERR] Invalid item" << endl;
            return;
        }

        string type = item["type"].get<string>();

        if (type == "group") {
            if (item.contains("template")) {
                string template_path = item["template"].get<string>();
                string name = item.value("name", "");
                if (!name.empty()) {
                    cout << "+-- GROUP (ref): " << name;
                }
                else {
                    cout << "+-- GROUP (ref): " << template_path;
                }
                cout << endl;

                if (item.contains("params") && item["params"].is_array()) {
                    cout << indent_str << "|   Params: [";
                    for (size_t i = 0; i < item["params"].size(); i++) {
                        if (i > 0) cout << ", ";
                        string val = item["params"][i].dump();
                        if (g_config.fill_params) {
                            val = replacePlaceholders(val, g_config.param_values);
                        }
                        cout << val;
                    }
                    cout << "]" << endl;
                }

                if (item.contains("confirm_between") && item["confirm_between"].is_boolean()) {
                    cout << indent_str << "|   Confirm Between: " << (item["confirm_between"].get<bool>() ? "true" : "false") << endl;
                }
                if (item.contains("delay_between") && item["delay_between"].is_number()) {
                    cout << indent_str << "|   Delay Between: " << item["delay_between"].get<int>() << " ms" << endl;
                }

                cout << indent_str << "+--" << endl;
            }
            else {
                string name = item.value("name", "unnamed");
                string mode = item.value("mode", "sequence");
                cout << "+-- GROUP (inline): " << name << " [mode: " << mode << "]" << endl;

                if (item.contains("body") && item["body"].is_array()) {
                    cout << indent_str << "|   Body: " << item["body"].size() << " items" << endl;
                    for (size_t i = 0; i < item["body"].size(); i++) {
                        printBodyItem(item["body"][i], indent + 4, i);
                    }
                }

                cout << indent_str << "+--" << endl;
            }
        }
        else {
            cout << "Unknown type: " << type << endl;
        }
    }

    static void printFlowDetails(const L4Flow& flow) {
        cout << endl;
        printSubheader(flow.getFilename());

        cout << "  Name: " << flow.getName() << endl;
        if (!flow.getDescription().empty()) {
            cout << "  Description: " << flow.getDescription() << endl;
        }
        cout << "  Version: " << flow.getVersion() << endl;
        cout << "  Timeout: " << flow.getTimeoutMs() << " ms";
        if (flow.getTimeoutMs() == 0) {
            cout << " (no timeout limit)";
        }
        cout << endl;

        const auto& profiles = flow.getProfiles();
        if (!profiles.empty()) {
            cout << endl;
            cout << "  Profiles:" << endl;
            for (const auto& [name, profile] : profiles) {
                cout << "    [" << name << "]";
                if (!profile.description.empty()) {
                    cout << " " << profile.description;
                }
                cout << endl;
                if (!profile.params.empty()) {
                    cout << "      Params:" << endl;
                    for (const auto& [key, value] : profile.params) {
                        cout << "        " << key << " = " << value << endl;
                        g_config.param_values["profile." + key] = value;
                    }
                }
            }
        }

        if (!flow.getActiveProfile().empty()) {
            cout << "  Active Profile: " << flow.getActiveProfile() << endl;
        }

        const auto& on_timeout = flow.getOnTimeout();
        if (!on_timeout.is_null() && on_timeout.is_array() && !on_timeout.empty()) {
            cout << endl;
            cout << "  On Timeout (" << on_timeout.size() << " items)" << endl;
        }

        if (flow.hasEmergencyCleanup()) {
            const auto& cleanup = flow.getEmergencyCleanup().value();
            cout << endl;
            cout << "  Emergency Cleanup:" << endl;
            cout << "    Type: " << cleanup.type << endl;
            if (!cleanup.body.is_null() && cleanup.body.is_array()) {
                cout << "    Body: " << cleanup.body.size() << " items" << endl;
            }
        }

        const auto& body = flow.getBody();
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

        if (arg == "--level=flow") {
            g_config.level = DisplayLevel::FLOW;
            cout << "[CONFIG] Display level: FLOW (show flow only)" << endl;
        }
        else if (arg == "--level=group") {
            g_config.level = DisplayLevel::GROUP;
            cout << "[CONFIG] Display level: GROUP (show flow + groups)" << endl;
        }
        else if (arg == "--level=full") {
            g_config.level = DisplayLevel::FULL;
            cout << "[CONFIG] Display level: FULL (show all details)" << endl;
        }
        else if (arg.rfind("--fill=", 0) == 0) {
            string val = arg.substr(7);
            g_config.fill_params = (val == "true" || val == "1" || val == "yes");
            cout << "[CONFIG] Fill parameters: " << (g_config.fill_params ? "true" : "false") << endl;
        }
        else if (arg.rfind("--param=", 0) == 0) {
            string kv = arg.substr(8);
            size_t eq_pos = kv.find('=');
            if (eq_pos != string::npos) {
                string key = kv.substr(0, eq_pos);
                string value = kv.substr(eq_pos + 1);
                g_config.param_values[key] = value;
                cout << "[CONFIG] Param override: " << key << "=" << value << endl;
            }
        }
        else if (arg[0] != '-') {
            base_dir = arg;
        }
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
        fs::current_path().string() + "/examples/config_examples",
    };

    for (const auto& path : possible_paths) {
        if (fs::exists(path) && fs::is_directory(path)) {
            return path;
        }
    }

    fs::path current = fs::path(exe_dir);
    for (int i = 0; i < 6; i++) {
        current = current.parent_path();
        string test_path = (current / "examples" / "config_examples").string();
        if (fs::exists(test_path) && fs::is_directory(test_path)) {
            return test_path;
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

    parseCommandLine(argc, argv, base_dir);

    if (base_dir.empty()) {
        string exe_dir = getExecutablePath();
        base_dir = findConfigDirectory(exe_dir);
        cout << "[DIR] Using default directory: " << base_dir << endl;
    }
    else {
        if (fs::path(base_dir).is_relative()) {
            base_dir = fs::absolute(base_dir).string();
        }
        cout << "[DIR] Using user-specified directory: " << base_dir << endl;
    }

    string l1_dir = base_dir + "/L1_action";
    string l2_dir = base_dir + "/L2_node";
    string l3_dir = base_dir + "/L3_group";
    string l4_dir = base_dir + "/L4_flow";

    cout << endl;
    OutputFormatter::printHeader("L4 Flow Loader Demo");
    cout << "  Base Directory: " << base_dir << endl;
    cout << "  L1 Action Directory: " << l1_dir << endl;
    cout << "  L2 Node Directory: " << l2_dir << endl;
    cout << "  L3 Group Directory: " << l3_dir << endl;
    cout << "  L4 Flow Directory: " << l4_dir << endl;
    cout << endl;

    if (!fs::exists(base_dir)) {
        cerr << "[ERR] Base directory does not exist: " << base_dir << endl;
        return 1;
    }

    try {
        // ============================================================
        // Step 1: 加载 L1 Actions
        // ============================================================
        cout << "[STEP] Step 1: Loading L1 Actions..." << endl;
        L1ActionCache l1_cache;
        if (fs::exists(l1_dir) && fs::is_directory(l1_dir)) {
            l1_cache.loadActionsFromDirectory(l1_dir);
        }
        else {
            cerr << "  [WARN] L1 action directory does not exist: " << l1_dir << endl;
        }
        l1_cache.printStats();

        // ============================================================
        // Step 2: 加载 L2 Nodes
        // ============================================================
        cout << "[STEP] Step 2: Loading L2 Nodes..." << endl;
        L2NodeCache l2_cache;
        if (fs::exists(l2_dir) && fs::is_directory(l2_dir)) {
            l2_cache.loadNodesFromDirectory(l2_dir);
        }
        else {
            cerr << "  [WARN] L2 node directory does not exist: " << l2_dir << endl;
        }
        l2_cache.printStats();

        // ============================================================
        // Step 3: 加载 L3 Groups
        // ============================================================
        cout << "[STEP] Step 3: Loading L3 Groups..." << endl;
        L3GroupCache l3_cache;
        if (fs::exists(l3_dir) && fs::is_directory(l3_dir)) {
            l3_cache.loadGroupsFromDirectory(l3_dir);
        }
        else {
            cerr << "  [WARN] L3 group directory does not exist: " << l3_dir << endl;
        }
        l3_cache.printStats();

        // ============================================================
        // Step 4: 加载 L4 Flows
        // ============================================================
        cout << "[STEP] Step 4: Loading L4 Flows..." << endl;
        vector<L4Flow> flows;

        if (fs::exists(l4_dir) && fs::is_directory(l4_dir)) {
            cout << "[DIR] Scanning L4 flow directory: " << l4_dir << endl;
            L4FlowLoader loader;
            flows = loader.loadFromDirectory(l4_dir);
            for (auto& flow : flows) {
                cout << "  [OK] Loaded flow: " << flow.getFilename() << " (" << flow.getName() << ")" << endl;
            }
        }
        else {
            cerr << "  [WARN] L4 flow directory does not exist: " << l4_dir << endl;
            cout << "  [INFO] Creating inline example flow..." << endl;

            json example_flow;
            example_flow["type"] = "flow";
            example_flow["name"] = "示例生产流程";
            example_flow["description"] = "演示流程";
            example_flow["version"] = "1.0.0";
            example_flow["timeout_ms"] = 3600000;

            json body = json::array();
            json step1;
            step1["type"] = "group";
            step1["name"] = "步骤1: 检查气源";
            step1["template"] = "L3_group/leak_test/prepare_stage.group";
            step1["params"] = json::array({ 1, 0x0101, 0x0201 });
            body.push_back(step1);

            json step2;
            step2["type"] = "group";
            step2["name"] = "步骤2: 充气";
            step2["template"] = "L3_group/leak_test/fill_stage.group";
            step2["params"] = json::array({ 1, 100.0, 10000, 5.0 });
            body.push_back(step2);

            json step3;
            step3["type"] = "group";
            step3["name"] = "步骤3: 保压";
            step3["template"] = "L3_group/leak_test/dwell_stage.group";
            step3["params"] = json::array({ 1, 5000, 0.1 });
            body.push_back(step3);

            example_flow["body"] = body;

            L4Flow flow("example_flow", example_flow);
            if (flow.isInitialized()) {
                flows.push_back(flow);
                cout << "  [OK] Created inline flow: example_flow" << endl;
            }
        }

        cout << "  [STAT] Total flows loaded: " << flows.size() << endl;
        cout << endl;

        // ============================================================
        // Step 5: 显示每个 Flow 的详细信息
        // ============================================================
        cout << endl;
        OutputFormatter::printSeparator('=');
        cout << "  L4 Flows Loaded: " << flows.size() << endl;
        OutputFormatter::printSeparator('=');

        if (!flows.empty()) {
            int index = 1;
            for (auto& flow : flows) {
                cout << endl;
                cout << "[" << index++ << "/" << flows.size() << "]";
                OutputFormatter::printFlowDetails(flow);
            }
        }
        else {
            cout << endl;
            cout << "  [WARN] No flows loaded." << endl;
            cout << "  [INFO] Please ensure L4_flow directory contains valid flow files." << endl;
            cout << "  [INFO] Expected file pattern: *_flow.json" << endl;
        }

        // ============================================================
        // Step 6: 完成
        // ============================================================
        cout << endl;
        OutputFormatter::printHeader("加载完成");
        cout << "  Total L1 Actions cached: " << l1_cache.getCacheKeys().size() << endl;
        cout << "  Total L2 Nodes cached: " << l2_cache.getCacheKeys().size() << endl;
        cout << "  Total L3 Groups cached: " << l3_cache.getCacheKeys().size() << endl;
        cout << "  Total L4 Flows loaded: " << flows.size() << endl;
        cout << endl;

    }
    catch (const exception& e) {
        cerr << endl;
        cerr << "[ERR] " << e.what() << endl;
        return 1;
    }

    return 0;
}