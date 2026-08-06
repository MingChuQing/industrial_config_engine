// demos/l2_node_loader_demo.cpp

#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <map>
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
#include "industrial_config_engine/types.hpp"

using namespace std;
using namespace industrial_config_engine;
using json = nlohmann::json;
namespace fs = std::filesystem;

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

    string resolveTemplateRef(const string& template_path) const {
        return template_path;
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

        if (!cache_.empty()) {
            cout << endl;
            cout << "  Cached actions:" << endl;
            for (const auto& [key, action] : cache_) {
                cout << "    - " << key << " (" << action.getType() << ")" << endl;
            }
        }
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
// 分支 Action 解析结果
// ============================================================
struct ResolvedBranchAction {
    enum class Type { TEMPLATE, INLINE };
    Type type;
    string ref;
    L1Action inline_action;
    const L1Action* cached_action = nullptr;
};

// ============================================================
// L2 Node 加载器
// ============================================================
class L2NodeLoader {
public:
    L2NodeLoader(const L1ActionCache& cache) : cache_(cache) {}

    vector<L2Node> loadNodes(const string& bundle_path) {
        vector<L2Node> nodes;

        if (!fs::exists(bundle_path)) {
            throw runtime_error("File not found: " + bundle_path);
        }

        json root = loadJsonFile(bundle_path);

        if (!root.contains("type") || root["type"].get<string>() != "node_bundle") {
            throw runtime_error("Not a valid node_bundle file");
        }

        json nodes_json = root["nodes"];
        cout << "📦 Loading " << nodes_json.size() << " nodes from bundle..." << endl;

        for (auto& node_json : nodes_json) {
            string filename = node_json.value("filename", "");
            L2Node node(filename, node_json);

            if (node.isInitialized()) {
                nodes.push_back(node);
            }
            else {
                cerr << "  ⚠ Warning: Failed to parse node: " << filename << endl;
            }
        }

        cout << "  ✅ Loaded " << nodes.size() << " nodes" << endl;
        return nodes;
    }

    // 解析 L2Node 的 action
    pair<bool, const L1Action*> resolveActionRef(const L2Node& node) const {
        const ActionRef& action_ref = node.getAction();

        // 如果是 inline action，返回 false（不从缓存查找）
        if (node.isActionInline()) {
            return { false, nullptr };
        }

        if (action_ref.template_path.empty()) {
            return { false, nullptr };
        }

        string cache_key = cache_.resolveTemplateRef(action_ref.template_path);
        const L1Action* action = cache_.getAction(cache_key);

        if (action != nullptr) {
            return { true, action };
        }

        string filename = fs::path(action_ref.template_path).filename().string();
        action = cache_.getActionByFilename(filename);
        if (action != nullptr) {
            return { true, action };
        }

        string basename = filename;
        size_t dot_pos = basename.find_last_of('.');
        if (dot_pos != string::npos) {
            basename = basename.substr(0, dot_pos);
        }
        action = cache_.getActionByBasename(basename);
        if (action != nullptr) {
            return { true, action };
        }

        return { false, nullptr };
    }

    // 从 inline JSON 字符串创建 L1Action
    L1Action createInlineAction(const string& inline_json) const {
        try {
            json action_json = json::parse(inline_json);
            return L1Action(action_json);
        }
        catch (const exception& e) {
            return L1Action();
        }
    }

    ResolvedBranchAction resolveBranchAction(const json& action_json) const {
        ResolvedBranchAction result;

        if (action_json.contains("template")) {
            result.type = ResolvedBranchAction::Type::TEMPLATE;
            result.ref = action_json["template"].get<string>();

            string cache_key = cache_.resolveTemplateRef(result.ref);
            result.cached_action = cache_.getAction(cache_key);

            if (result.cached_action == nullptr) {
                string filename = fs::path(result.ref).filename().string();
                result.cached_action = cache_.getActionByFilename(filename);

                if (result.cached_action == nullptr) {
                    string basename = filename;
                    size_t dot_pos = basename.find_last_of('.');
                    if (dot_pos != string::npos) {
                        basename = basename.substr(0, dot_pos);
                    }
                    result.cached_action = cache_.getActionByBasename(basename);
                }

                if (result.cached_action == nullptr) {
                    cerr << "    ⚠ Warning: Action not found in cache: " << result.ref << endl;
                }
            }
        }
        else {
            result.type = ResolvedBranchAction::Type::INLINE;
            result.ref = "(inline)";

            try {
                result.inline_action = L1Action(action_json);
            }
            catch (const exception& e) {
                cerr << "    ⚠ Warning: Failed to parse inline action: " << e.what() << endl;
            }
        }

        return result;
    }

private:
    const L1ActionCache& cache_;

    json loadJsonFile(const string& path) {
        ifstream file(path);
        if (!file.is_open()) {
            throw runtime_error("Cannot open file: " + path);
        }
        json root;
        file >> root;
        return root;
    }
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

    static void printL1ActionDetails(const L1Action& action, int indent = 4, const string& ref = "") {
        string indent_str(indent, ' ');
        string indent2(indent + 2, ' ');

        if (!action.isInitialized()) {
            cout << indent_str << "⚠ Action not initialized" << endl;
            return;
        }

        cout << indent_str << "┌─ L1 Action" << endl;

        if (!ref.empty() && ref != "(inline)") {
            cout << indent_str << "│  Source: " << ref << endl;
        }

        if (!action.getFilename().empty()) {
            cout << indent_str << "│  Filename: " << action.getFilename() << endl;
        }
        if (!action.getType().empty()) {
            cout << indent_str << "│  Type: " << action.getType() << endl;
        }
        if (!action.getDescription().empty()) {
            cout << indent_str << "│  Description: " << action.getDescription() << endl;
        }
        if (!action.getProtocol().empty()) {
            cout << indent_str << "│  Protocol: " << action.getProtocol() << endl;
        }
        if (!action.getRequest().empty()) {
            cout << indent_str << "│  Request: " << action.getRequest() << endl;
        }
        if (!action.getResponse().empty()) {
            cout << indent_str << "│  Response: " << action.getResponse() << endl;
        }

        if (action.getTimeoutMs() > 0) {
            cout << indent_str << "│  Timeout: " << action.getTimeoutMs() << " ms" << endl;

        }
       
        const ParseConfig* parse = action.getParse();
        if (parse != nullptr) {
            cout << indent_str << "│  Parse:" << endl;
            cout << indent2 << "├─ start_byte: " << parse->start_byte << endl;
            cout << indent2 << "├─ length: " << parse->length << endl;
            cout << indent2 << "├─ endian: " << parse->endian << endl;
            cout << indent2 << "└─ type: " << parse->type << endl;
        }

        const CheckConfig* check = action.getCheck();
        if (check != nullptr) {
            cout << indent_str << "│  Check:" << endl;
            cout << indent2 << "├─ condition: " << check->condition << endl;
            cout << indent2 << "└─ value: " << check->value << endl;
        }

        if (action.hasExpression()) {
            cout << indent_str << "│  Expression: " << action.getExpression().value_or("") << endl;
        }

        if (action.hasDuration()) {
            cout << indent_str << "│  Duration: " << action.getDuration().value_or("") << endl;
            if (!action.getUnit().empty()) {
                cout << indent_str << "│  Unit: " << action.getUnit() << endl;
            }
        }

        const auto& args = action.getArgs();
        if (!args.empty()) {
            cout << indent_str << "│  Args:" << endl;
            for (size_t i = 0; i < args.size(); i++) {
                const ArgDef& arg = args[i];
                cout << indent2 << "├─ [" << i << "] ";
                cout << "name=" << arg.name;
                cout << ", type=" << arg.getTypeName();
                if (!arg.desc.empty()) {
                    cout << ", desc=" << arg.desc;
                }
                if (arg.min_val.has_value()) {
                    cout << ", min=" << arg.min_val.value();
                }
                if (arg.max_val.has_value()) {
                    cout << ", max=" << arg.max_val.value();
                }
                if (arg.default_value.has_value()) {
                    cout << ", default=" << arg.default_value.value();
                }
                if (!arg.enum_values.empty()) {
                    cout << ", enum=[";
                    for (size_t j = 0; j < arg.enum_values.size(); j++) {
                        if (j > 0) cout << ",";
                        cout << arg.enum_values[j];
                    }
                    cout << "]";
                }
                cout << endl;
            }
        }

        cout << indent_str << "└─────────────────" << endl;
    }

    static void printInlineActionDetails(const json& action_json, int indent = 4) {
        string indent_str(indent, ' ');
        string indent2(indent + 2, ' ');

        cout << indent_str << "┌─ Inline Action" << endl;
        if (action_json.contains("type")) {
            cout << indent_str << "│  Type: " << action_json["type"].get<string>() << endl;
        }
        if (action_json.contains("message")) {
            cout << indent_str << "│  Message: " << action_json["message"].get<string>() << endl;
        }
        if (action_json.contains("level")) {
            cout << indent_str << "│  Level: " << action_json["level"].get<string>() << endl;
        }
        if (action_json.contains("params") && action_json["params"].is_array()) {
            cout << indent_str << "│  Params: [";
            for (size_t i = 0; i < action_json["params"].size(); i++) {
                if (i > 0) cout << ", ";
                cout << action_json["params"][i].get<string>();
            }
            cout << "]" << endl;
        }
        if (action_json.contains("action")) {
            cout << indent_str << "│  UI Action: " << action_json["action"].get<string>() << endl;
            if (action_json.contains("data") && action_json["data"].is_object()) {
                cout << indent_str << "│  Data:" << endl;
                for (auto& [key, value] : action_json["data"].items()) {
                    cout << indent2 << "├─ " << key << ": " << value.get<string>() << endl;
                }
            }
        }
        // ✅ 新增：显示 request/response 等信息
        if (action_json.contains("protocol")) {
            cout << indent_str << "│  Protocol: " << action_json["protocol"].get<string>() << endl;
        }
        if (action_json.contains("request")) {
            cout << indent_str << "│  Request: " << action_json["request"].get<string>() << endl;
        }
        if (action_json.contains("response")) {
            cout << indent_str << "│  Response: " << action_json["response"].get<string>() << endl;
        }
        if (action_json.contains("description")) {
            cout << indent_str << "│  Description: " << action_json["description"].get<string>() << endl;
        }
        if (action_json.contains("timeout_ms")) {
            cout << indent_str << "│  Timeout: " << action_json["timeout_ms"].get<int>() << " ms" << endl;
        }
        cout << indent_str << "└─────────────────" << endl;
    }

    static void printL2NodeDetails(
        const L2Node& node,
        const L1ActionCache& cache,
        const L2NodeLoader& loader
    ) {
        cout << endl;
        printSubheader(node.getFilename());

        cout << "  Name: " << node.getName() << endl;
        if (node.hasDescription()) {
            cout << "  Description: " << node.getDescription() << endl;
        }
        cout << "  Timeout: " << node.getTimeoutMs() << " ms" << endl;
        cout << "  Max Retries: " << (int)node.getMaxRetries() << endl;
        cout << "  Retry Interval: " << node.getRetryInterval() << " ms" << endl;

        const auto& params = node.getParams();
        if (!params.empty()) {
            cout << "  Params: [";
            for (size_t i = 0; i < params.size(); i++) {
                if (i > 0) cout << ", ";
                cout << params[i].dump();
            }
            cout << "]" << endl;
        }

        // ✅ 打印 Action - 区分 template 和 inline
        cout << endl;
        if (node.isActionInline()) {
            // inline action
            cout << "  ACTION (inline):" << endl;
            try {
                json action_json = json::parse(node.getInlineActionJson());
                printInlineActionDetails(action_json, 4);
            }
            catch (...) {
                cout << "    ⚠ Failed to parse inline action JSON" << endl;
            }
        }
        else {
            // template 引用
            const ActionRef& action_ref = node.getAction();
            if (action_ref.template_path.empty()) {
                cout << "  ACTION: (none)" << endl;
            }
            else {
                cout << "  ACTION (template): " << action_ref.template_path << endl;
                auto [found, action] = loader.resolveActionRef(node);
                if (found && action != nullptr) {
                    printL1ActionDetails(*action, 4, action_ref.template_path);
                }
                else {
                    cout << "    ⚠ Action not found in cache" << endl;
                }
            }
        }

        const auto& judge = node.getJudge();
        if (judge.has_value() && judge->type != JudgeType::NONE) {
            cout << endl;
            cout << "  JUDGE:" << endl;
            switch (judge->type) {
            case JudgeType::COMPARE:
                cout << "    type: compare" << endl;
                cout << "    condition: " << judge->condition << endl;
                cout << "    value: " << judge->value << endl;
                break;
            case JudgeType::EXPRESSION:
                cout << "    type: expression" << endl;
                cout << "    expression: " << judge->expression << endl;
                break;
            case JudgeType::EXISTS:
                cout << "    type: exists" << endl;
                break;
            default:
                break;
            }
        }

        const auto& on_success = node.getOnSuccess();
        if (!on_success.empty()) {
            cout << endl;
            cout << "  ON_SUCCESS (" << on_success.size() << " actions):" << endl;
            for (size_t i = 0; i < on_success.size(); i++) {
                const json& action_json = on_success[i];
                cout << "    [" << i << "] ";
                auto resolved = loader.resolveBranchAction(action_json);

                if (resolved.type == ResolvedBranchAction::Type::TEMPLATE) {
                    cout << "template: " << resolved.ref << endl;
                    if (resolved.cached_action != nullptr) {
                        printL1ActionDetails(*resolved.cached_action, 8, resolved.ref);
                    }
                    else {
                        cout << "        ⚠ Action not found in cache" << endl;
                    }
                }
                else {
                    cout << "(inline)" << endl;
                    if (resolved.inline_action.isInitialized()) {
                        printL1ActionDetails(resolved.inline_action, 8);
                    }
                    else {
                        printInlineActionDetails(action_json, 8);
                    }
                }
            }
        }

        const auto& on_failure = node.getOnFailure();
        if (!on_failure.empty()) {
            cout << endl;
            cout << "  ON_FAILURE (" << on_failure.size() << " actions):" << endl;
            for (size_t i = 0; i < on_failure.size(); i++) {
                const json& action_json = on_failure[i];
                cout << "    [" << i << "] ";
                auto resolved = loader.resolveBranchAction(action_json);

                if (resolved.type == ResolvedBranchAction::Type::TEMPLATE) {
                    cout << "template: " << resolved.ref << endl;
                    if (resolved.cached_action != nullptr) {
                        printL1ActionDetails(*resolved.cached_action, 8, resolved.ref);
                    }
                    else {
                        cout << "        ⚠ Action not found in cache" << endl;
                    }
                }
                else {
                    cout << "(inline)" << endl;
                    if (resolved.inline_action.isInitialized()) {
                        printL1ActionDetails(resolved.inline_action, 8);
                    }
                    else {
                        printInlineActionDetails(action_json, 8);
                    }
                }
            }
        }

        // ✅ 新增：打印 On Timeout
        const auto& on_timeout = node.getOnTimeout();
        if (!on_timeout.empty()) {
            cout << endl;
            cout << "  ON_TIMEOUT (" << on_timeout.size() << " actions):" << endl;
            for (size_t i = 0; i < on_timeout.size(); i++) {
                const json& action_json = on_timeout[i];
                cout << "    [" << i << "] ";
                auto resolved = loader.resolveBranchAction(action_json);

                if (resolved.type == ResolvedBranchAction::Type::TEMPLATE) {
                    cout << "template: " << resolved.ref << endl;
                    if (resolved.cached_action != nullptr) {
                        printL1ActionDetails(*resolved.cached_action, 8, resolved.ref);
                    }
                    else {
                        cout << "        ⚠ Action not found in cache" << endl;
                    }
                }
                else {
                    cout << "(inline)" << endl;
                    if (resolved.inline_action.isInitialized()) {
                        printL1ActionDetails(resolved.inline_action, 8);
                    }
                    else {
                        printInlineActionDetails(action_json, 8);
                    }
                }
            }
        }

        printSeparator('-');
    }
};

// ============================================================
// 路径工具函数
// ============================================================

string getExecutablePath() {
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
// 主函数
// ============================================================
int main(int argc, char* argv[]) {
    string base_dir;

    if (argc > 1) {
        base_dir = argv[1];
        if (fs::path(base_dir).is_relative()) {
            base_dir = fs::absolute(base_dir).string();
        }
        cout << "📂 Using user-specified directory: " << base_dir << endl;
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

        cout << "📂 Using default directory: " << base_dir << endl;
    }

    string l1_dir = base_dir + "/L1_action";
    string l2_dir = base_dir + "/L2_node";
    string l2_bundle_path = l2_dir + "/leadshine_idm/all_nodes.json";

    cout << endl;
    OutputFormatter::printHeader("L2 Node Loader Demo (with L1 Action Cache)");
    cout << "  Base Directory: " << base_dir << endl;
    cout << "  L1 Action Directory: " << l1_dir << endl;
    cout << "  L2 Node Bundle: " << l2_bundle_path << endl;
    cout << endl;

    if (!fs::exists(base_dir)) {
        cerr << "❌ Error: Base directory does not exist: " << base_dir << endl;
        cerr << "   Please create the directory or specify correct path." << endl;
        return 1;
    }

    try {
        cout << "📂 Step 1: Loading L1 Actions..." << endl;
        L1ActionCache cache;

        if (fs::exists(l1_dir) && fs::is_directory(l1_dir)) {
            cache.loadActionsFromDirectory(l1_dir);
        }
        else {
            cerr << "  ⚠ L1 action directory does not exist: " << l1_dir << endl;
        }
        cache.printStats();

        cout << "📂 Step 2: Loading L2 Nodes..." << endl;
        L2NodeLoader loader(cache);
        vector<L2Node> nodes;

        if (fs::exists(l2_bundle_path)) {
            nodes = loader.loadNodes(l2_bundle_path);
        }
        else {
            cerr << "  ⚠ L2 node bundle does not exist: " << l2_bundle_path << endl;
            cerr << "  ⚠ Looking for L2_node in: " << l2_dir << endl;

            if (fs::exists(l2_dir) && fs::is_directory(l2_dir)) {
                try {
                    for (const auto& entry : fs::recursive_directory_iterator(l2_dir)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".json") {
                            cout << "  ℹ Found L2 bundle: " << entry.path().string() << endl;
                            nodes = loader.loadNodes(entry.path().string());
                            break;
                        }
                    }
                }
                catch (const exception& e) {
                    cerr << "  ⚠ Failed to scan L2_node directory: " << e.what() << endl;
                }
            }
        }

        cout << endl;
        OutputFormatter::printSeparator('=');
        cout << "  L2 Nodes Loaded: " << nodes.size() << endl;
        OutputFormatter::printSeparator('=');

        if (!nodes.empty()) {
            int index = 1;
            for (auto& node : nodes) {
                cout << endl;
                cout << "[" << index++ << "/" << nodes.size() << "]";
                OutputFormatter::printL2NodeDetails(node, cache, loader);
            }
        }
        else {
            cout << endl;
            cout << "  ⚠ No nodes loaded." << endl;
            cout << "  💡 Please ensure L2_node directory contains valid node bundles." << endl;
        }

        cout << endl;
        OutputFormatter::printHeader("加载完成");
        cout << "  Total L1 Actions cached: " << cache.getCacheKeys().size() << endl;
        cout << "  Total L2 Nodes loaded: " << nodes.size() << endl;
        cout << endl;

    }
    catch (const exception& e) {
        cerr << endl;
        cerr << "❌ Error: " << e.what() << endl;
        return 1;
    }

    return 0;
}