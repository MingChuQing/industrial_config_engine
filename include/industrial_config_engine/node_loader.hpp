// include/industrial_config_engine/node_loader.hpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <nlohmann/json.hpp>
#include "l2_node.hpp"
#include "config.hpp"

namespace industrial_config_engine {

    // ============================================================
    // 回调函数类型定义
    // ============================================================

    using NodeProgressCallback = std::function<void(const std::string&, int, int)>;
    using NodeErrorCallback = std::function<void(const std::string&, const std::string&)>;
    using NodeLoadedCallback = std::function<void(const std::string&, const L2Node&)>;

    // ============================================================
    // NodeLoader 类
    // ============================================================

    class NodeLoader {
    public:
        NodeLoader();
        ~NodeLoader();

        // ============================================================
        // 回调设置
        // ============================================================

        void setProgressCallback(NodeProgressCallback callback) { progress_callback_ = callback; }
        void setErrorCallback(NodeErrorCallback callback) { error_callback_ = callback; }
        void setNodeLoadedCallback(NodeLoadedCallback callback) { node_loaded_callback_ = callback; }

        // ============================================================
        // 加载接口
        // ============================================================

        // 加载单个 Node 文件
        bool loadSingleFile(const std::string& filepath);

        // 加载目录（递归）
        bool loadDirectory(const std::string& directory_path, bool recursive = true);

        // 加载多个路径
        bool loadPaths(const std::vector<std::string>& paths, bool recursive = true);

        // 从JSON字符串加载（用于测试）
        bool loadFromJsonString(const std::string& json_str, const std::string& virtual_path);

        // 从JSON对象加载
        bool loadFromJson(const nlohmann::json& json, const std::string& virtual_path);

        // ============================================================
        // 查询接口
        // ============================================================

        const L2Node* getNode(const std::string& key) const;
        L2Node* getNode(const std::string& key);
        bool hasNode(const std::string& key) const;
        std::vector<std::string> getNodeKeys() const;
        const std::unordered_map<std::string, L2Node>& getAllNodes() const { return nodes_; }

        // 按前缀查找
        std::vector<std::string> findNodesByPrefix(const std::string& prefix) const;

        // 按名称查找
        std::vector<std::string> findNodesByName(const std::string& name) const;

        // ============================================================
        // 统计信息
        // ============================================================

        size_t getNodeCount() const { return nodes_.size(); }
        size_t getLoadedFileCount() const { return loaded_files_; }
        size_t getErrorCount() const { return error_count_; }

        // ============================================================
        // 管理接口
        // ============================================================

        void clear();
        bool removeNode(const std::string& key);
        void setStrictMode(bool strict) { strict_mode_ = strict; }
        bool getStrictMode() const { return strict_mode_; }
        void setAllowOverwrite(bool allow) { allow_overwrite_ = allow; }
        bool getAllowOverwrite() const { return allow_overwrite_; }

    private:
        // ============================================================
        // 内部加载方法
        // ============================================================

        bool loadJsonFile(const std::string& filepath, const std::string& base_path);
        bool loadSingleNodeFromJson(const nlohmann::json& json,
            const std::string& base_path,
            const std::string& filename);
        bool loadNodeBundleFromJson(const nlohmann::json& json,
            const std::string& base_path,
            const std::string& bundle_filename);

        // ============================================================
        // 辅助方法
        // ============================================================

        std::string makeKey(const std::string& base_path,
            const std::string& bundle_name,
            const std::string& filename) const;
        std::string normalizePath(const std::string& path) const;
        bool isNodeFile(const nlohmann::json& json) const;
        bool isNodeBundleFile(const nlohmann::json& json) const;
        void traverseDirectory(const std::string& directory_path,
            const std::string& base_path,
            std::vector<std::string>& files);
        std::string getBaseName(const std::string& filename) const;
        bool isSupportedExtension(const std::string& ext) const;

    private:
        // ============================================================
        // 成员变量
        // ============================================================

        // Node 存储: key -> L2Node
        std::unordered_map<std::string, L2Node> nodes_;

        // 统计信息
        size_t loaded_files_ = 0;
        size_t error_count_ = 0;

        // 配置
        bool strict_mode_ = true;
        bool allow_overwrite_ = false;

        // 回调
        NodeProgressCallback progress_callback_;
        NodeErrorCallback error_callback_;
        NodeLoadedCallback node_loaded_callback_;

        // 支持的扩展名
        static const std::vector<std::string> supported_extensions_;
    };

    // ============================================================
    // 流输出操作符
    // ============================================================

    std::ostream& operator<<(std::ostream& os, const NodeLoader& loader);

} // namespace industrial_config_engine