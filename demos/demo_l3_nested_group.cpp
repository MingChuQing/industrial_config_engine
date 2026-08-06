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
#include "industrial_config_engine/types.hpp"

using namespace std;
using namespace industrial_config_engine;
using json = nlohmann::json;
namespace fs = std::filesystem;

// ============================================================
// L1 Action Cache Manager
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
            cout << "  [STATS] Total actions cached: " << cache_.size() << endl;
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
        cout << "[STATS] L1 Action Cache Stats:" << endl;
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
// L2 Node Cache Manager
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
            cout << "  [STATS] Total nodes cached: " << cache_.size() << endl;
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
        cout << "[STATS] L2 Node Cache Stats:" << endl;
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
// Output Formatting Utilities
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

    static void printBodyItem(const json& item, int indent = 4, int index = -1) {
        string indent_str(indent, ' ');

        if (index >= 0) {
            cout << indent_str << "[" << index << "] ";
        }

        if (!item.is_object() || !item.contains("type")) {
            cout << "[WARN] Invalid item" << endl;
            return;
        }

        string type = item["type"].get<string>();

        if (type == "node") {
            string name = item.value("name", "unnamed");
            cout << "NODE: " << name;
            if (item.contains("result_key")) {
                cout << " -> " << item["result_key"].get<string>();
            }
            cout << endl;

            if (item.contains("node") && item["node"].is_object()) {
                if (item["node"].contains("template")) {
                    string template_path = item["node"]["template"].get<string>();
                    cout << indent_str << "  Template: " << template_path << endl;
                }
            }

        }
        else if (type == "group") {
            string name = item.value("name", "unnamed");
            string mode = item.value("mode", "sequence");
            cout << "GROUP: " << name << " (mode: " << mode << ")" << endl;

            if (item.contains("body") && item["body"].is_array()) {
                cout << indent_str << "  Body: " << item["body"].size() << " items" << endl;
                for (size_t i = 0; i < item["body"].size(); i++) {
                    printBodyItem(item["body"][i], indent + 4, i);
                }
            }
        }
        else {
            cout << "Unknown type: " << type << endl;
        }
    }

    static void printGroupDetails(const L3Group& group, int depth = 0) {
        string depth_str(depth * 2, ' ');

        cout << endl;
        printSubheader(depth_str + group.getFilename());

        cout << depth_str << "  Name: " << group.getName() << endl;
        if (!group.getDescription().empty()) {
            cout << depth_str << "  Description: " << group.getDescription() << endl;
        }
        cout << depth_str << "  Mode: " << modeToString(group.getMode()) << endl;
        cout << depth_str << "  Max Nodes: " << group.getMaxNodes() << endl;
        cout << depth_str << "  Total Nodes Count: " << group.countNodes() << endl;

        const auto& args = group.getArgs();
        if (!args.empty()) {
            cout << endl;
            cout << depth_str << "  Args:" << endl;
            for (const auto& arg : args) {
                cout << depth_str << "    [" << arg.index << "] " << arg.name << ": " << dataTypeToString(arg.type);
                if (!arg.desc.empty()) {
                    cout << " - " << arg.desc;
                }
                cout << endl;
            }
        }

        if (group.hasLoop()) {
            const auto& loop = group.getLoop().value();
            cout << endl;
            cout << depth_str << "  Loop:" << endl;
            cout << depth_str << "    Type: " << loopTypeToString(loop.type) << endl;
            if (loop.type == LoopType::COUNT) {
                cout << depth_str << "    Count: " << loop.count << endl;
            }
            else if (loop.type == LoopType::WHILE || loop.type == LoopType::UNTIL) {
                cout << depth_str << "    Condition: " << loop.condition << endl;
            }
        }

        if (group.hasCondition()) {
            const auto& cond = group.getCondition().value();
            cout << endl;
            cout << depth_str << "  Condition:" << endl;
            cout << depth_str << "    Type: " << cond.type << endl;
            if (!cond.expression.empty()) {
                cout << depth_str << "    Expression: " << cond.expression << endl;
            }
        }

        if (group.hasThen()) {
            const auto& then_body = group.getThen();
            cout << endl;
            cout << depth_str << "  Then (" << then_body.size() << " items):" << endl;
            for (size_t i = 0; i < then_body.size(); i++) {
                printBodyItem(then_body[i], depth + 4, i);
            }
        }

        if (group.hasElse()) {
            const auto& else_body = group.getElse();
            cout << endl;
            cout << depth_str << "  Else (" << else_body.size() << " items):" << endl;
            for (size_t i = 0; i < else_body.size(); i++) {
                printBodyItem(else_body[i], depth + 4, i);
            }
        }

        if (group.hasCases()) {
            const auto& cases = group.getCases();
            cout << endl;
            cout << depth_str << "  Cases (" << cases.size() << " branches):" << endl;
            for (size_t i = 0; i < cases.size(); i++) {
                const auto& c = cases[i];
                cout << depth_str << "    [" << i << "] case: " << c.case_value << endl;
                if (c.body.is_array()) {
                    cout << depth_str << "        Body: " << c.body.size() << " items" << endl;
                }
            }
        }

        if (group.hasDefault()) {
            const auto& default_body = group.getDefault();
            cout << endl;
            cout << depth_str << "  Default (" << default_body.size() << " items)" << endl;
        }

        const auto& body = group.getBody();
        if (!body.is_null() && body.is_array()) {
            cout << endl;
            cout << depth_str << "  Body (" << body.size() << " items):" << endl;
            for (size_t i = 0; i < body.size(); i++) {
                printBodyItem(body[i], depth + 4, i);
            }
        }

        printSeparator('-');
    }
};

// ============================================================
// Path Utility Functions
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

// ============================================================
// Create Test Group with Nesting (for demonstration)
// ============================================================

L3Group createNestedTestGroup() {
    // Create inner Group
    json inner_group;
    inner_group["type"] = "group";
    inner_group["name"] = "Inner Parallel Group";
    inner_group["description"] = "Execute multiple operations in parallel";
    inner_group["mode"] = "parallel";
    inner_group["max_nodes"] = 10;

    json inner_body = json::array();

    // Inner Node 1
    json node1;
    node1["type"] = "node";
    node1["name"] = "Read Sensor A";
    node1["result_key"] = "sensor_a";
    node1["params"] = json::object();
    node1["params"]["instance_id"] = "${instance_id}";
    node1["timeout_ms"] = 2000;
    node1["max_retries"] = 3;
    node1["retry_interval"] = 500;
    node1["node"]["template"] = "L2_node/read_status.r_u16_motor_status";
    node1["judge"]["type"] = "exists";
    node1["on_success"] = json::array();
    node1["on_failure"] = json::array();

    // Inner Node 2
    json node2;
    node2["type"] = "node";
    node2["name"] = "Read Sensor B";
    node2["result_key"] = "sensor_b";
    node2["params"] = json::object();
    node2["params"]["instance_id"] = "${instance_id}";
    node2["timeout_ms"] = 2000;
    node2["max_retries"] = 3;
    node2["retry_interval"] = 500;
    node2["node"]["template"] = "L2_node/read_status.r_u16_motor_status";
    node2["judge"]["type"] = "exists";
    node2["on_success"] = json::array();
    node2["on_failure"] = json::array();

    inner_body.push_back(node1);
    inner_body.push_back(node2);
    inner_group["body"] = inner_body;

    // Create outer Group
    json outer_group;
    outer_group["type"] = "group";
    outer_group["name"] = "Outer Sequence Group (Nesting Demo)";
    outer_group["description"] = "Sequential execution with nested parallel group";
    outer_group["mode"] = "sequence";
    outer_group["max_nodes"] = 20;
    outer_group["delay_between"] = 500;
    outer_group["confirm_between"] = false;

    json outer_body = json::array();

    // Outer Node 1
    json outer_node1;
    outer_node1["type"] = "node";
    outer_node1["name"] = "Initialize Device";
    outer_node1["result_key"] = "init_result";
    outer_node1["params"] = json::object();
    outer_node1["params"]["instance_id"] = "${instance_id}";
    outer_node1["timeout_ms"] = 3000;
    outer_node1["max_retries"] = 3;
    outer_node1["retry_interval"] = 500;
    outer_node1["node"]["template"] = "L2_node/motor_enable.r_b_enable_result";
    outer_node1["judge"]["type"] = "compare";
    outer_node1["judge"]["condition"] = "eq";
    outer_node1["judge"]["value"] = true;
    outer_node1["on_success"] = json::array();
    outer_node1["on_failure"] = json::array();

    // Nested inner Group
    outer_body.push_back(outer_node1);
    outer_body.push_back(inner_group);

    // Outer Node 2
    json outer_node2;
    outer_node2["type"] = "node";
    outer_node2["name"] = "Summarize Results";
    outer_node2["result_key"] = "summary";
    outer_node2["params"] = json::object();
    outer_node2["params"]["instance_id"] = "${instance_id}";
    outer_node2["timeout_ms"] = 2000;
    outer_node2["max_retries"] = 0;
    outer_node2["retry_interval"] = 0;
    outer_node2["node"]["template"] = "L2_node/read_status.r_u16_motor_status";
    outer_node2["judge"]["type"] = "exists";
    outer_node2["on_success"] = json::array();
    outer_node2["on_failure"] = json::array();

    outer_body.push_back(outer_node2);
    outer_group["body"] = outer_body;

    outer_group["args"] = json::array();
    json arg;
    arg["index"] = 0;
    arg["name"] = "instance_id";
    arg["type"] = "s";
    arg["desc"] = "Device instance ID";
    outer_group["args"].push_back(arg);

    // Load as L3Group
    L3Group group("nested_demo.group", outer_group);
    return group;
}

// ============================================================
// Main Function
// ============================================================
int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    string base_dir;

    if (argc > 1) {
        base_dir = argv[1];
        if (fs::path(base_dir).is_relative()) {
            base_dir = fs::absolute(base_dir).string();
        }
        cout << "[DIR] Using user-specified directory: " << base_dir << endl;
    }
    else {
        string exe_dir = getExecutablePath();
        base_dir = exe_dir + "/examples/config_examples";

        if (!fs::exists(base_dir)) {
            base_dir = fs::current_path().string() + "/examples/config_examples";
        }

        if (!fs::exists(base_dir)) {
            base_dir = fs::current_path().string();
        }

        cout << "[DIR] Using default directory: " << base_dir << endl;
    }

    string l1_dir = base_dir + "/L1_action";
    string l2_dir = base_dir + "/L2_node";
    string l3_dir = base_dir + "/L3_group";

    cout << endl;
    OutputFormatter::printHeader("L3 Nested Group Demo");
    cout << "  Base Directory: " << base_dir << endl;
    cout << "  L1 Action Directory: " << l1_dir << endl;
    cout << "  L2 Node Directory: " << l2_dir << endl;
    cout << "  L3 Group Directory: " << l3_dir << endl;
    cout << endl;

    if (!fs::exists(base_dir)) {
        cerr << "[ERROR] Base directory does not exist: " << base_dir << endl;
        return 1;
    }

    try {
        // ============================================================
        // Step 1: Load L1 Actions
        // ============================================================
        cout << "[DIR] Step 1: Loading L1 Actions..." << endl;
        L1ActionCache l1_cache;
        if (fs::exists(l1_dir) && fs::is_directory(l1_dir)) {
            l1_cache.loadActionsFromDirectory(l1_dir);
        }
        else {
            cerr << "  [WARN] L1 action directory does not exist: " << l1_dir << endl;
        }
        l1_cache.printStats();

        // ============================================================
        // Step 2: Load L2 Nodes
        // ============================================================
        cout << "[DIR] Step 2: Loading L2 Nodes..." << endl;
        L2NodeCache l2_cache;
        if (fs::exists(l2_dir) && fs::is_directory(l2_dir)) {
            l2_cache.loadNodesFromDirectory(l2_dir);
        }
        else {
            cerr << "  [WARN] L2 node directory does not exist: " << l2_dir << endl;
        }
        l2_cache.printStats();

        // ============================================================
        // Step 3: Create nested Group for demonstration
        // ============================================================
        cout << "[DIR] Step 3: Creating nested Group for demonstration..." << endl;
        cout << endl;

        L3Group nested_group = createNestedTestGroup();

        if (nested_group.isInitialized()) {
            cout << "  [OK] Created nested Group: " << nested_group.getName() << endl;
            cout << "  [STATS] Total nodes: " << nested_group.countNodes() << endl;
            cout << "  [STATS] Max nodes: " << nested_group.getMaxNodes() << endl;
        }
        else {
            cerr << "  [ERROR] Failed to create nested Group" << endl;
            return 1;
        }

        // ============================================================
        // Step 4: Display Group structure tree
        // ============================================================
        cout << endl;
        OutputFormatter::printHeader("Group Structure Tree");
        cout << endl;

        cout << "[DIR] " << nested_group.getName() << " (mode: "
            << OutputFormatter::modeToString(nested_group.getMode())
            << ", nodes: " << nested_group.countNodes() << ")" << endl;

        const auto& body = nested_group.getBody();
        if (!body.is_null() && body.is_array()) {
            for (size_t i = 0; i < body.size(); i++) {
                const auto& item = body[i];
                if (item.is_object() && item.contains("type")) {
                    string type = item["type"].get<string>();
                    string name = item.value("name", "unnamed");

                    if (type == "node") {
                        cout << "  +-- [NODE] " << name << endl;
                    }
                    else if (type == "group") {
                        string mode = item.value("mode", "sequence");
                        cout << "  +-- [GROUP] " << name << " (mode: " << mode << ")" << endl;

                        // Print inner Group's child nodes
                        if (item.contains("body") && item["body"].is_array()) {
                            const auto& inner_body = item["body"];
                            for (size_t j = 0; j < inner_body.size(); j++) {
                                const auto& inner_item = inner_body[j];
                                if (inner_item.is_object() && inner_item.contains("type")) {
                                    string inner_type = inner_item["type"].get<string>();
                                    string inner_name = inner_item.value("name", "unnamed");

                                    if (inner_type == "node") {
                                        string prefix = (j == inner_body.size() - 1) ? "    +-- " : "    +-- ";
                                        cout << "  |  " << prefix << "[NODE] " << inner_name << endl;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ============================================================
        // Step 5: Display detailed Group information
        // ============================================================
        cout << endl;
        OutputFormatter::printHeader("Nested Group Details");
        OutputFormatter::printGroupDetails(nested_group);

        // ============================================================
        // Step 6: Display Groups loaded from files (if any)
        // ============================================================
        if (fs::exists(l3_dir) && fs::is_directory(l3_dir)) {
            cout << endl;
            OutputFormatter::printHeader("L3 Groups Loaded from Files");

            vector<L3Group> groups;
            try {
                for (const auto& entry : fs::recursive_directory_iterator(l3_dir)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".json") {
                        string path = entry.path().string();
                        if (path.find(".group.") != string::npos ||
                            path.find(".group.json") != string::npos) {
                            L3Group group;
                            if (group.loadFromFile(path)) {
                                groups.push_back(group);
                                cout << "  [OK] Loaded: " << group.getFilename() << " (" << group.getName() << ")" << endl;
                            }
                        }
                    }
                }
            }
            catch (const exception& e) {
                cerr << "  [WARN] Failed to scan L3_group directory: " << e.what() << endl;
            }

            cout << endl;
            cout << "  [STATS] Total groups from file: " << groups.size() << endl;

            if (!groups.empty()) {
                cout << endl;
                cout << "  Group hierarchy:" << endl;
                for (auto& group : groups) {
                    cout << "  [DIR] " << group.getName() << " ("
                        << OutputFormatter::modeToString(group.getMode())
                        << ", " << group.countNodes() << " nodes)" << endl;
                }
            }
        }

        // ============================================================
        // Step 7: Complete
        // ============================================================
        cout << endl;
        OutputFormatter::printHeader("Demo Complete");
        cout << "  L1 Actions cached: " << l1_cache.getCacheKeys().size() << endl;
        cout << "  L2 Nodes cached: " << l2_cache.getCacheKeys().size() << endl;
        cout << endl;
        cout << "  [INFO] Nested Group Demo Notes:" << endl;
        cout << "     1. Outer Group uses 'sequence' mode for sequential execution" << endl;
        cout << "     2. Inner Group uses 'parallel' mode for parallel execution" << endl;
        cout << "     3. Supports arbitrary depth of nesting" << endl;
        cout << "     4. max_nodes limits the maximum number of nodes" << endl;
        cout << endl;

    }
    catch (const exception& e) {
        cerr << endl;
        cerr << "[ERROR] " << e.what() << endl;
        return 1;
    }

    return 0;
}