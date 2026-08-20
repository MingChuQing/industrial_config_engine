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
    // callback function type definitions
    // ============================================================

    using NodeProgressCallback = std::function<void(const std::string&, int, int)>;
    using NodeErrorCallback = std::function<void(const std::string&, const std::string&)>;
    using NodeLoadedCallback = std::function<void(const std::string&, const L2Node&)>;

    // ============================================================
    // NodeLoader class
    // ============================================================

    class NodeLoader {
    public:
        NodeLoader();
        ~NodeLoader();

        // ============================================================
        // callback setters
        // ============================================================

        void setProgressCallback(NodeProgressCallback callback) { progress_callback_ = callback; }
        void setErrorCallback(NodeErrorCallback callback) { error_callback_ = callback; }
        void setNodeLoadedCallback(NodeLoadedCallback callback) { node_loaded_callback_ = callback; }

        // ============================================================
        // load interface
        // ============================================================

        // load single Node file
        bool loadSingleFile(const std::string& filepath);

        // load directory (recursive)
        bool loadDirectory(const std::string& directory_path, bool recursive = true);

        // load multiple paths
        bool loadPaths(const std::vector<std::string>& paths, bool recursive = true);

        // load from JSON string (for testing)
        bool loadFromJsonString(const std::string& json_str, const std::string& virtual_path);

        // load from JSON object
        bool loadFromJson(const nlohmann::json& json, const std::string& virtual_path);

        // ============================================================
        // query interface
        // ============================================================

        const L2Node* getNode(const std::string& key) const;
        L2Node* getNode(const std::string& key);
        bool hasNode(const std::string& key) const;
        std::vector<std::string> getNodeKeys() const;
        const std::unordered_map<std::string, L2Node>& getAllNodes() const { return nodes_; }

        // find by prefix
        std::vector<std::string> findNodesByPrefix(const std::string& prefix) const;

        // find by name
        std::vector<std::string> findNodesByName(const std::string& name) const;

        // ============================================================
        // statistics
        // ============================================================

        size_t getNodeCount() const { return nodes_.size(); }
        size_t getLoadedFileCount() const { return loaded_files_; }
        size_t getErrorCount() const { return error_count_; }

        // ============================================================
        // management interface
        // ============================================================

        void clear();
        bool removeNode(const std::string& key);
        void setStrictMode(bool strict) { strict_mode_ = strict; }
        bool getStrictMode() const { return strict_mode_; }
        void setAllowOverwrite(bool allow) { allow_overwrite_ = allow; }
        bool getAllowOverwrite() const { return allow_overwrite_; }

    private:
        // ============================================================
        // internal load methods
        // ============================================================

        bool loadJsonFile(const std::string& filepath, const std::string& base_path);
        bool loadSingleNodeFromJson(const nlohmann::json& json,
            const std::string& base_path,
            const std::string& filename);
        bool loadNodeBundleFromJson(const nlohmann::json& json,
            const std::string& base_path,
            const std::string& bundle_filename);

        // ============================================================
        // helper methods
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
        // member variables
        // ============================================================

        // Node store: key -> L2Node
        std::unordered_map<std::string, L2Node> nodes_;

        // statistics
        size_t loaded_files_ = 0;
        size_t error_count_ = 0;

        // config
        bool strict_mode_ = true;
        bool allow_overwrite_ = false;

        // callback
        NodeProgressCallback progress_callback_;
        NodeErrorCallback error_callback_;
        NodeLoadedCallback node_loaded_callback_;

        // supported extensions
        static const std::vector<std::string> supported_extensions_;
    };

    // ============================================================
    // stream output operator
    // ============================================================

    std::ostream& operator<<(std::ostream& os, const NodeLoader& loader);

} // namespace industrial_config_engine