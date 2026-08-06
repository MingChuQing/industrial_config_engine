// src/node_loader.cpp
#include "industrial_config_engine/node_loader.hpp"
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

    const std::vector<std::string> NodeLoader::supported_extensions_ = { ".json", ".JSON" };

    // ============================================================
    // 构造函数和析构函数
    // ============================================================

    NodeLoader::NodeLoader() {
        const auto& config = GlobalConfig::getInstance().getConfig();
        strict_mode_ = config.strict_mode;
        allow_overwrite_ = config.allow_overwrite;
    }

    NodeLoader::~NodeLoader() {
        // Cleanup resources
    }

    // ============================================================
    // 加载接口实现
    // ============================================================

    bool NodeLoader::loadSingleFile(const std::string& filepath) {
        if (!fs::exists(filepath)) {
            if (error_callback_) {
                error_callback_(filepath, "File does not exist");
            }
            error_count_++;
            return false;
        }

        if (!fs::is_regular_file(filepath)) {
            if (error_callback_) {
                error_callback_(filepath, "Not a regular file");
            }
            error_count_++;
            return false;
        }

        std::string ext = fs::path(filepath).extension().string();
        if (!isSupportedExtension(ext)) {
            if (error_callback_) {
                error_callback_(filepath, "Unsupported file type: " + ext);
            }
            error_count_++;
            return false;
        }

        std::string parent_dir = fs::path(filepath).parent_path().string();
        std::string base_path = fs::path(parent_dir).filename().string();
        if (base_path.empty()) {
            base_path = ".";
        }

        return loadJsonFile(filepath, base_path);
    }

    bool NodeLoader::loadDirectory(const std::string& directory_path, bool recursive) {
        if (!fs::exists(directory_path) || !fs::is_directory(directory_path)) {
            if (error_callback_) {
                error_callback_(directory_path, "Directory does not exist or is not a directory");
            }
            error_count_++;
            return false;
        }

        std::vector<std::string> files;
        std::string base_path = fs::path(directory_path).filename().string();
        if (base_path.empty()) {
            base_path = fs::path(directory_path).parent_path().filename().string();
        }

        traverseDirectory(directory_path, base_path, files);

        if (files.empty()) {
            return true;
        }

        int total = static_cast<int>(files.size());
        int loaded = 0;

        for (const auto& file : files) {
            bool result = loadJsonFile(file, base_path);
            if (result) {
                loaded++;
            }
            if (progress_callback_) {
                progress_callback_(file, loaded, total);
            }
        }

        return true;
    }

    bool NodeLoader::loadPaths(const std::vector<std::string>& paths, bool recursive) {
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
                    error_callback_(path, "Path does not exist or is not a file/directory");
                }
                all_success = false;
            }
        }
        return all_success;
    }

    bool NodeLoader::loadFromJsonString(const std::string& json_str, const std::string& virtual_path) {
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

    bool NodeLoader::loadFromJson(const nlohmann::json& json, const std::string& virtual_path) {
        try {
            std::string base_path = fs::path(virtual_path).parent_path().string();
            if (base_path.empty() || base_path == ".") {
                base_path = "virtual";
            }

            std::string filename = fs::path(virtual_path).filename().string();
            if (filename.empty()) {
                filename = "virtual_node.json";
            }

            if (isNodeBundleFile(json)) {
                return loadNodeBundleFromJson(json, base_path, filename);
            }
            else {
                return loadSingleNodeFromJson(json, base_path, filename);
            }
        }
        catch (const std::exception& e) {
            if (error_callback_) {
                error_callback_(virtual_path, std::string("Load failed: ") + e.what());
            }
            error_count_++;
            return false;
        }
    }

    // ============================================================
    // 查询接口实现
    // ============================================================

    const L2Node* NodeLoader::getNode(const std::string& key) const {
        auto it = nodes_.find(key);
        if (it != nodes_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    L2Node* NodeLoader::getNode(const std::string& key) {
        auto it = nodes_.find(key);
        if (it != nodes_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    bool NodeLoader::hasNode(const std::string& key) const {
        return nodes_.find(key) != nodes_.end();
    }

    std::vector<std::string> NodeLoader::getNodeKeys() const {
        std::vector<std::string> keys;
        keys.reserve(nodes_.size());
        for (const auto& [key, _] : nodes_) {
            keys.push_back(key);
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    std::vector<std::string> NodeLoader::findNodesByPrefix(const std::string& prefix) const {
        std::vector<std::string> result;
        for (const auto& [key, _] : nodes_) {
            if (key.find(prefix) == 0) {
                result.push_back(key);
            }
        }
        return result;
    }

    std::vector<std::string> NodeLoader::findNodesByName(const std::string& name) const {
        std::vector<std::string> result;
        for (const auto& [key, node] : nodes_) {
            if (node.getName() == name) {
                result.push_back(key);
            }
        }
        return result;
    }

    // ============================================================
    // 管理接口实现
    // ============================================================

    void NodeLoader::clear() {
        nodes_.clear();
        loaded_files_ = 0;
        error_count_ = 0;
    }

    bool NodeLoader::removeNode(const std::string& key) {
        return nodes_.erase(key) > 0;
    }

    // ============================================================
    // 内部加载方法实现
    // ============================================================

    bool NodeLoader::loadJsonFile(const std::string& filepath, const std::string& base_path) {
        try {
            std::ifstream file(filepath);
            if (!file.is_open()) {
                if (error_callback_) {
                    error_callback_(filepath, "Unable to open file");
                }
                error_count_++;
                return false;
            }

            nlohmann::json json;
            file >> json;
            file.close();

            std::string filename = fs::path(filepath).filename().string();

            if (isNodeBundleFile(json)) {
                return loadNodeBundleFromJson(json, base_path, filename);
            }
            else if (isNodeFile(json)) {
                return loadSingleNodeFromJson(json, base_path, filename);
            }
            else {
                // Not a Node file, skip silently
                return false;
            }
        }
        catch (const std::exception& e) {
            if (error_callback_) {
                error_callback_(filepath, std::string("Load failed: ") + e.what());
            }
            error_count_++;
            return false;
        }
    }

    bool NodeLoader::loadSingleNodeFromJson(const nlohmann::json& json,
        const std::string& base_path,
        const std::string& filename) {
        try {
            std::string name_without_ext = getBaseName(filename);
            std::string key = makeKey(base_path, "", name_without_ext);

            // Check if already exists
            if (hasNode(key)) {
                if (!allow_overwrite_) {
                    if (error_callback_) {
                        error_callback_(key, "Node already exists, skipping (allow_overwrite=false)");
                    }
                    return false;
                }
            }

            L2Node node(name_without_ext, json);

            // Strict mode validation
            if (strict_mode_) {
                const auto& sig = node.getSignature();
                if (sig.name.empty()) {
                    if (error_callback_) {
                        error_callback_(filename, "Invalid Node signature");
                    }
                    error_count_++;
                    return false;
                }
            }

            nodes_[key] = std::move(node);
            loaded_files_++;

            if (node_loaded_callback_) {
                node_loaded_callback_(key, nodes_[key]);
            }

            return true;
        }
        catch (const std::exception& e) {
            if (error_callback_) {
                error_callback_(base_path + "/" + filename, std::string("Failed to load Node: ") + e.what());
            }
            error_count_++;
            return false;
        }
    }

    bool NodeLoader::loadNodeBundleFromJson(const nlohmann::json& json,
        const std::string& base_path,
        const std::string& bundle_filename) {
        try {
            if (!json.contains("nodes") || !json["nodes"].is_array()) {
                if (error_callback_) {
                    error_callback_(base_path + "/" + bundle_filename, "Bundle file missing 'nodes' array");
                }
                error_count_++;
                return false;
            }

            std::string bundle_name = getBaseName(bundle_filename);

            for (const auto& node_json : json["nodes"]) {
                if (!node_json.contains("filename") || !node_json["filename"].is_string()) {
                    if (error_callback_) {
                        error_callback_(base_path + "/" + bundle_filename, "Node missing 'filename' field");
                    }
                    error_count_++;
                    continue;
                }

                std::string node_filename = node_json["filename"].get<std::string>();
                std::string key = makeKey(base_path, bundle_name, node_filename);

                // Check if already exists
                if (hasNode(key)) {
                    if (!allow_overwrite_) {
                        if (error_callback_) {
                            error_callback_(key, "Node already exists, skipping");
                        }
                        continue;
                    }
                }

                L2Node node(node_filename, node_json);

                if (strict_mode_) {
                    const auto& sig = node.getSignature();
                    if (sig.name.empty()) {
                        if (error_callback_) {
                            error_callback_(node_filename, "Invalid Node signature");
                        }
                        error_count_++;
                        continue;
                    }
                }

                nodes_[key] = std::move(node);
                loaded_files_++;

                if (node_loaded_callback_) {
                    node_loaded_callback_(key, nodes_[key]);
                }
            }

            return true;
        }
        catch (const std::exception& e) {
            if (error_callback_) {
                error_callback_(base_path + "/" + bundle_filename, std::string("Failed to load Node bundle: ") + e.what());
            }
            error_count_++;
            return false;
        }
    }

    // ============================================================
    // 辅助方法实现
    // ============================================================

    std::string NodeLoader::makeKey(const std::string& base_path,
        const std::string& bundle_name,
        const std::string& filename) const {
        std::string key;

        if (!base_path.empty() && base_path != ".") {
            key = normalizePath(base_path);
        }

        if (!bundle_name.empty()) {
            if (!key.empty()) {
                key += "/";
            }
            key += bundle_name;
        }

        if (!filename.empty()) {
            if (!key.empty()) {
                key += "/";
            }
            key += filename;
        }

        return key;
    }

    std::string NodeLoader::normalizePath(const std::string& path) const {
        std::string result = path;
        std::replace(result.begin(), result.end(), '\\', '/');
        while (!result.empty() && result.back() == '/') {
            result.pop_back();
        }
        std::regex double_slash("/+");
        result = std::regex_replace(result, double_slash, "/");
        return result;
    }

    bool NodeLoader::isNodeFile(const nlohmann::json& json) const {
        if (!json.is_object()) {
            return false;
        }
        if (!json.contains("type") || !json["type"].is_string()) {
            return false;
        }
        std::string type = json["type"].get<std::string>();
        return type == "node";
    }

    bool NodeLoader::isNodeBundleFile(const nlohmann::json& json) const {
        if (!json.is_object()) {
            return false;
        }
        if (!json.contains("type") || !json["type"].is_string()) {
            return false;
        }
        std::string type = json["type"].get<std::string>();
        return type == "node_bundle";
    }

    void NodeLoader::traverseDirectory(const std::string& directory_path,
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
                error_callback_(directory_path, std::string("Failed to traverse directory: ") + e.what());
            }
        }
    }

    std::string NodeLoader::getBaseName(const std::string& filename) const {
        size_t dot_pos = filename.find_last_of('.');
        if (dot_pos != std::string::npos) {
            return filename.substr(0, dot_pos);
        }
        return filename;
    }

    bool NodeLoader::isSupportedExtension(const std::string& ext) const {
        return std::find(supported_extensions_.begin(),
            supported_extensions_.end(),
            ext) != supported_extensions_.end();
    }

    // ============================================================
    // 流输出操作符
    // ============================================================

    std::ostream& operator<<(std::ostream& os, const NodeLoader& loader) {
        os << "========================================" << std::endl;
        os << "NodeLoader Information" << std::endl;
        os << "========================================" << std::endl;
        os << "Total Nodes: " << loader.getNodeCount() << std::endl;
        os << "Files Loaded: " << loader.getLoadedFileCount() << std::endl;
        os << "Errors: " << loader.getErrorCount() << std::endl;
        os << "Strict Mode: " << (loader.getStrictMode() ? "Enabled" : "Disabled") << std::endl;
        os << "Allow Overwrite: " << (loader.getAllowOverwrite() ? "Yes" : "No") << std::endl;

        auto keys = loader.getNodeKeys();
        if (!keys.empty()) {
            os << "\nNode List:" << std::endl;
            for (const auto& key : keys) {
                os << "  - " << key << std::endl;
            }
        }

        os << "========================================" << std::endl;
        return os;
    }

} // namespace industrial_config_engine