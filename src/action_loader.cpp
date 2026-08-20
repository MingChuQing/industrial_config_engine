// src/action_loader.cpp
#include "industrial_config_engine/action_loader.hpp"
#include <fstream>
#include <filesystem>
#include <regex>
#include <algorithm>
#include <iostream>
#include <iomanip>

namespace industrial_config_engine {

    namespace fs = std::filesystem;

    // ============================================================
    // 静态成员初始化
    // ============================================================

    const std::vector<std::string> ActionLoader::supported_extensions_ = { ".json", ".JSON" };

    // ============================================================
    // 构造函数和析构函数
    // ============================================================

    ActionLoader::ActionLoader() {
        const auto& config = GlobalConfig::getInstance().getConfig();
        strict_mode_ = config.strict_mode;
        allow_overwrite_ = config.allow_overwrite;
    }

    ActionLoader::~ActionLoader() {
        // 清理资源
    }

    // ============================================================
    // 加载接口实现
    // ============================================================

    bool ActionLoader::loadSingleFile(const std::string& filepath) {
        if (!fs::exists(filepath)) {
            if (error_callback_) {
                error_callback_(filepath, "file does not exist");
            }
            error_count_++;
            return false;
        }

        if (!fs::is_regular_file(filepath)) {
            if (error_callback_) {
                error_callback_(filepath, "not a regular file");
            }
            error_count_++;
            return false;
        }

        // 检查扩展名
        std::string ext = fs::path(filepath).extension().string();
        if (!isSupportedExtension(ext)) {
            if (error_callback_) {
                error_callback_(filepath, "unsupported file type: " + ext);
            }
            error_count_++;
            return false;
        }

        // 获取父目录作为base_path
        std::string parent_dir = fs::path(filepath).parent_path().string();
        std::string base_path = fs::path(parent_dir).filename().string();
        if (base_path.empty()) {
            base_path = ".";
        }

        return loadJsonFile(filepath, base_path);
    }

    bool ActionLoader::loadDirectory(const std::string& directory_path, bool recursive) {
        if (!fs::exists(directory_path) || !fs::is_directory(directory_path)) {
            if (error_callback_) {
                error_callback_(directory_path, "directory does not exist or is not a directory");
            }
            error_count_++;
            return false;
        }

        std::vector<std::string> files;
        std::string base_path = fs::path(directory_path).filename().string();
        if (base_path.empty()) {
            base_path = fs::path(directory_path).parent_path().filename().string();
        }

        if (recursive) {
            traverseDirectory(directory_path, base_path, files);
        }
        else {
            // 只遍历当前目录
            try {
                for (const auto& entry : fs::directory_iterator(directory_path)) {
                    if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        if (isSupportedExtension(ext)) {
                            files.push_back(entry.path().string());
                        }
                    }
                }
            }
            catch (const std::exception& e) {
                if (error_callback_) {
                    error_callback_(directory_path, std::string("failed to iterate directory: ") + e.what());
                }
                error_count_++;
                return false;
            }
        }

        if (files.empty()) {
            return true;
        }

        int total = static_cast<int>(files.size());
        int loaded = 0;

        for (const auto& file : files) {
            // 按文件所在子目录计算 base_path（如 L2_node/leak_test）
            std::string file_base = base_path;
            try {
                fs::path rel = fs::relative(fs::path(file), fs::path(directory_path));
                if (rel.has_parent_path()) {
                    std::string parent = rel.parent_path().generic_string();
                    if (!parent.empty() && parent != ".") file_base += "/" + parent;
                }
            } catch (...) {}
            bool result = loadJsonFile(file, file_base);
            if (result) {
                loaded++;
            }
            if (progress_callback_) {
                progress_callback_(file, loaded, total);
            }
        }

        return true;
    }

    bool ActionLoader::loadPaths(const std::vector<std::string>& paths, bool recursive) {
        bool all_success = true;
        for (const auto& path : paths) {
            if (fs::is_directory(path)) {
                if (!loadDirectory(path, recursive)) {
                    all_success = false;
                }
            }
            else if (fs::is_regular_file(path)) {
                if (!loadSingleFile(path)) {
                    all_success = false;
                }
            }
            else {
                if (error_callback_) {
                    error_callback_(path, "path does not exist or is not a file/directory");
                }
                all_success = false;
            }
        }
        return all_success;
    }

    bool ActionLoader::loadFromJsonString(const std::string& json_str, const std::string& virtual_path) {
        try {
            nlohmann::json json = nlohmann::json::parse(json_str);
            return loadFromJson(json, virtual_path);
        }
        catch (const std::exception& e) {
            if (error_callback_) {
                error_callback_(virtual_path, std::string("JSON parse error: ") + e.what());
            }
            error_count_++;
            return false;
        }
    }

    bool ActionLoader::loadFromJson(const nlohmann::json& json, const std::string& virtual_path) {
        try {
            // 确定base_path
            std::string base_path = fs::path(virtual_path).parent_path().string();
            if (base_path.empty() || base_path == ".") {
                base_path = "virtual";
            }

            std::string filename = fs::path(virtual_path).filename().string();
            if (filename.empty()) {
                filename = "virtual_action.json";
            }

            if (isBundleFile(json)) {
                return loadBundleFromJson(json, base_path, filename);
            }
            else {
                return loadSingleActionFromJson(json, base_path, filename);
            }
        }
        catch (const std::exception& e) {
            if (error_callback_) {
                error_callback_(virtual_path, std::string("load failed: ") + e.what());
            }
            error_count_++;
            return false;
        }
    }

    // ============================================================
    // 查询接口实现
    // ============================================================

    const L1Action* ActionLoader::getAction(const std::string& key) const {
        auto it = actions_.find(key);
        if (it != actions_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    L1Action* ActionLoader::getAction(const std::string& key) {
        auto it = actions_.find(key);
        if (it != actions_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    bool ActionLoader::hasAction(const std::string& key) const {
        return actions_.find(key) != actions_.end();
    }

    std::vector<std::string> ActionLoader::getActionKeys() const {
        std::vector<std::string> keys;
        keys.reserve(actions_.size());
        for (const auto& [key, _] : actions_) {
            keys.push_back(key);
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    std::vector<std::string> ActionLoader::findActionsByPrefix(const std::string& prefix) const {
        std::vector<std::string> result;
        for (const auto& [key, _] : actions_) {
            if (key.find(prefix) == 0) {
                result.push_back(key);
            }
        }
        return result;
    }

    std::vector<std::string> ActionLoader::findActionsByType(const std::string& type) const {
        std::vector<std::string> result;
        for (const auto& [key, action] : actions_) {
            if (action.getType() == type) {
                result.push_back(key);
            }
        }
        return result;
    }

    std::vector<std::string> ActionLoader::findActionsByProtocol(const std::string& protocol) const {
        std::vector<std::string> result;
        for (const auto& [key, action] : actions_) {
            if (action.getProtocol() == protocol) {
                result.push_back(key);
            }
        }
        return result;
    }

    // ============================================================
    // 统计信息实现
    // ============================================================

    std::unordered_map<std::string, int> ActionLoader::getTypeStatistics() const {
        std::unordered_map<std::string, int> stats;
        for (const auto& [_, action] : actions_) {
            stats[action.getType()]++;
        }
        return stats;
    }

    std::unordered_map<std::string, int> ActionLoader::getProtocolStatistics() const {
        std::unordered_map<std::string, int> stats;
        for (const auto& [_, action] : actions_) {
            std::string protocol = action.getProtocol();
            if (!protocol.empty()) {
                stats[protocol]++;
            }
        }
        return stats;
    }

    // ============================================================
    // 管理接口实现
    // ============================================================

    void ActionLoader::clear() {
        actions_.clear();
        loaded_files_ = 0;
        error_count_ = 0;
    }

    bool ActionLoader::removeAction(const std::string& key) {
        return actions_.erase(key) > 0;
    }

    // ============================================================
    // 内部加载方法实现
    // ============================================================

    bool ActionLoader::loadJsonFile(const std::string& filepath, const std::string& base_path) {
        try {
            std::ifstream file(filepath);
            if (!file.is_open()) {
                if (error_callback_) {
                    error_callback_(filepath, "cannot open file");
                }
                error_count_++;
                return false;
            }

            nlohmann::json json;
            file >> json;
            file.close();

            std::string filename = fs::path(filepath).filename().string();

            if (isBundleFile(json)) {
                return loadBundleFromJson(json, base_path, filename);
            }
            else {
                return loadSingleActionFromJson(json, base_path, filename);
            }
        }
        catch (const std::exception& e) {
            if (error_callback_) {
                error_callback_(filepath, std::string("load failed: ") + e.what());
            }
            error_count_++;
            return false;
        }
    }

    bool ActionLoader::loadSingleActionFromJson(const nlohmann::json& json,
        const std::string& base_path,
        const std::string& filename) {
        try {
            std::string name_without_ext = getBaseName(filename);
            std::string key = makeKey(base_path, "", name_without_ext);

            // 检查是否已存在
            if (hasAction(key)) {
                if (!allow_overwrite_) {
                    if (error_callback_) {
                        error_callback_(key, "Action already exists, skipping load (allow_overwrite=false)");
                    }
                    return false;
                }
            }

            L1Action action(name_without_ext, json);

            // 严格模式验证
            if (strict_mode_) {
                // 验证签名是否匹配
                const auto& sig = action.getSignature();
                if (sig.name.empty()) {
                    if (error_callback_) {
                        error_callback_(filename, "invalid Action signature");
                    }
                    error_count_++;
                    return false;
                }
            }

            actions_[key] = std::move(action);
            loaded_files_++;

            // 触发回调
            if (action_loaded_callback_) {
                action_loaded_callback_(key, actions_[key]);
            }

            return true;
        }
        catch (const std::exception& e) {
            if (error_callback_) {
                error_callback_(base_path + "/" + filename, std::string("failed to load Action: ") + e.what());
            }
            error_count_++;
            return false;
        }
    }

    bool ActionLoader::loadBundleFromJson(const nlohmann::json& json,
        const std::string& base_path,
        const std::string& bundle_filename) {
        try {
            if (!json.contains("actions") || !json["actions"].is_array()) {
                if (error_callback_) {
                    error_callback_(base_path + "/" + bundle_filename, "bundle file missing actions array");
                }
                error_count_++;
                return false;
            }

            std::string bundle_name = getBaseName(bundle_filename);

            int loaded_count = 0;
            int total_actions = static_cast<int>(json["actions"].size());

            for (const auto& action_json : json["actions"]) {
                if (!action_json.contains("filename") || !action_json["filename"].is_string()) {
                    if (error_callback_) {
                        error_callback_(base_path + "/" + bundle_filename, "action missing filename field");
                    }
                    error_count_++;
                    continue;
                }

                std::string action_filename = action_json["filename"].get<std::string>();
                std::string key = makeKey(base_path, bundle_name, action_filename);

                // 检查是否已存在
                if (hasAction(key)) {
                    if (!allow_overwrite_) {
                        if (error_callback_) {
                            error_callback_(key, "Action already exists, skipping load");
                        }
                        continue;
                    }
                }

                L1Action action(action_filename, action_json);

                // 严格模式验证
                if (strict_mode_) {
                    const auto& sig = action.getSignature();
                    if (sig.name.empty()) {
                        if (error_callback_) {
                            error_callback_(action_filename, "invalid Action signature");
                        }
                        error_count_++;
                        continue;
                    }
                }

                actions_[key] = std::move(action);
                loaded_count++;

                if (action_loaded_callback_) {
                    action_loaded_callback_(key, actions_[key]);
                }
            }

            loaded_files_++;
            return true;
        }
        catch (const std::exception& e) {
            if (error_callback_) {
                error_callback_(base_path + "/" + bundle_filename, std::string("failed to load bundle file: ") + e.what());
            }
            error_count_++;
            return false;
        }
    }

    // ============================================================
    // 辅助方法实现
    // ============================================================

    std::string ActionLoader::makeKey(const std::string& base_path,
        const std::string& bundle_name,
        const std::string& filename) const {
        std::string key;

        // 添加base_path
        if (!base_path.empty() && base_path != ".") {
            key = normalizePath(base_path);
        }

        // 添加bundle_name（如果是聚合文件）
        if (!bundle_name.empty()) {
            if (!key.empty()) {
                key += "/";
            }
            key += bundle_name;
        }

        // 添加filename
        if (!filename.empty()) {
            if (!key.empty()) {
                key += "/";
            }
            key += filename;
        }

        return key;
    }

    std::string ActionLoader::normalizePath(const std::string& path) const {
        std::string result = path;
        // 替换反斜杠为正斜杠
        std::replace(result.begin(), result.end(), '\\', '/');
        // 移除末尾斜杠
        while (!result.empty() && result.back() == '/') {
            result.pop_back();
        }
        // 移除重复斜杠
        std::regex double_slash("/+");
        result = std::regex_replace(result, double_slash, "/");
        return result;
    }

    bool ActionLoader::isBundleFile(const nlohmann::json& json) const {
        if (!json.is_object()) {
            return false;
        }
        if (!json.contains("type") || !json["type"].is_string()) {
            return false;
        }
        std::string type = json["type"].get<std::string>();
        return type == "action_bundle";
    }

    void ActionLoader::traverseDirectory(const std::string& directory_path,
        const std::string& base_path,
        std::vector<std::string>& files) {
        try {
            for (const auto& entry : fs::recursive_directory_iterator(directory_path)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    if (isSupportedExtension(ext)) {
                        files.push_back(entry.path().string());
                    }
                }
            }
        }
        catch (const std::exception& e) {
            if (error_callback_) {
                error_callback_(directory_path, std::string("failed to iterate directory: ") + e.what());
            }
        }
    }

    std::string ActionLoader::getBaseName(const std::string& filename) const {
        size_t dot_pos = filename.find_last_of('.');
        if (dot_pos != std::string::npos) {
            return filename.substr(0, dot_pos);
        }
        return filename;
    }

    bool ActionLoader::isSupportedExtension(const std::string& ext) const {
        return std::find(supported_extensions_.begin(),
            supported_extensions_.end(),
            ext) != supported_extensions_.end();
    }

    // ============================================================
    // 流输出操作符
    // ============================================================

    std::ostream& operator<<(std::ostream& os, const ActionLoader& loader) {
        os << "========================================" << std::endl;
        os << "ActionLoader info" << std::endl;
        os << "========================================" << std::endl;
        os << "Action total: " << loader.getActionCount() << std::endl;
        os << "Loaded files: " << loader.getLoadedFileCount() << std::endl;
        os << "Errors: " << loader.getErrorCount() << std::endl;
        os << "Strict mode: " << (loader.getStrictMode() ? "on" : "off") << std::endl;
        os << "Allow overwrite: " << (loader.getAllowOverwrite() ? "yes" : "no") << std::endl;

        // 类型统计
        auto type_stats = loader.getTypeStatistics();
        if (!type_stats.empty()) {
            os << "\nType statistics:" << std::endl;
            for (const auto& [type, count] : type_stats) {
                os << "  " << type << ": " << count << "" << std::endl;
            }
        }

        // Action列表
        auto keys = loader.getActionKeys();
        if (!keys.empty()) {
            os << "\nAction list:" << std::endl;
            for (const auto& key : keys) {
                os << "  - " << key << std::endl;
            }
        }

        os << "========================================" << std::endl;
        return os;
    }

} // namespace industrial_config_engine