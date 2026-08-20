// include/industrial_config_engine/action_loader.hpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include "l1_action.hpp"
#include "config.hpp"

namespace industrial_config_engine {

    // ============================================================
    // callback function type definitions
    // ============================================================

    // load progress callback: (file path, loaded count, total count)
    using LoadProgressCallback = std::function<void(const std::string&, int, int)>;

    // load error callback: (file path, error message)
    using LoadErrorCallback = std::function<void(const std::string&, const std::string&)>;

    // Action load complete callback: (key, Action pointer)
    using ActionLoadedCallback = std::function<void(const std::string&, const L1Action&)>;

    // ============================================================
    // ActionLoader class
    // ============================================================

    class ActionLoader {
    public:
        ActionLoader();
        ~ActionLoader();

        // ============================================================
        // callback setters
        // ============================================================

        void setProgressCallback(LoadProgressCallback callback) { progress_callback_ = callback; }
        void setErrorCallback(LoadErrorCallback callback) { error_callback_ = callback; }
        void setActionLoadedCallback(ActionLoadedCallback callback) { action_loaded_callback_ = callback; }

        // ============================================================
        // load interface
        // ============================================================

        // load single file
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

        // get Action (const version)
        const L1Action* getAction(const std::string& key) const;

        // get Action (non-const version)
        L1Action* getAction(const std::string& key);

        // check existence
        bool hasAction(const std::string& key) const;

        // get all keys (sorted)
        std::vector<std::string> getActionKeys() const;

        // get all Actions
        const std::unordered_map<std::string, L1Action>& getAllActions() const { return actions_; }

        // find by prefix
        std::vector<std::string> findActionsByPrefix(const std::string& prefix) const;

        // find by type
        std::vector<std::string> findActionsByType(const std::string& type) const;

        // find by protocol
        std::vector<std::string> findActionsByProtocol(const std::string& protocol) const;

        // ============================================================
        // statistics
        // ============================================================

        size_t getActionCount() const { return actions_.size(); }
        size_t getLoadedFileCount() const { return loaded_files_; }
        size_t getErrorCount() const { return error_count_; }

        // get type statistics
        std::unordered_map<std::string, int> getTypeStatistics() const;

        // get protocol statistics
        std::unordered_map<std::string, int> getProtocolStatistics() const;

        // ============================================================
        // management interface
        // ============================================================

        // clear all Actions
        void clear();

        // remove specified Action
        bool removeAction(const std::string& key);

        // set strict mode (strict validation)
        void setStrictMode(bool strict) { strict_mode_ = strict; }
        bool getStrictMode() const { return strict_mode_; }

        // set whether overwrite allowed
        void setAllowOverwrite(bool allow) { allow_overwrite_ = allow; }
        bool getAllowOverwrite() const { return allow_overwrite_; }

    private:
        // ============================================================
        // internal load methods
        // ============================================================

        // load JSON file (auto-detect single or bundle)
        bool loadJsonFile(const std::string& filepath, const std::string& base_path);

        // load single Action
        bool loadSingleActionFromJson(const nlohmann::json& json,
            const std::string& base_path,
            const std::string& filename);

        // load bundle file (action_bundle)
        bool loadBundleFromJson(const nlohmann::json& json,
            const std::string& base_path,
            const std::string& bundle_filename);

        // ============================================================
        // helper methods
        // ============================================================

        // generate unique key
        std::string makeKey(const std::string& base_path,
            const std::string& bundle_name,
            const std::string& filename) const;

        // normalize path
        std::string normalizePath(const std::string& path) const;

        // check if bundle file
        bool isBundleFile(const nlohmann::json& json) const;

        // recursively walk directory
        void traverseDirectory(const std::string& directory_path,
            const std::string& base_path,
            std::vector<std::string>& files);

        // extract filename (without extension)
        std::string getBaseName(const std::string& filename) const;

        // check if file extension supported
        bool isSupportedExtension(const std::string& ext) const;

    private:
        // ============================================================
        // member variables
        // ============================================================

        // Action store: key -> L1Action
        std::unordered_map<std::string, L1Action> actions_;

        // statistics
        size_t loaded_files_ = 0;
        size_t error_count_ = 0;

        // config
        bool strict_mode_ = true;
        bool allow_overwrite_ = false;

        // callback
        LoadProgressCallback progress_callback_;
        LoadErrorCallback error_callback_;
        ActionLoadedCallback action_loaded_callback_;

        // supported extensions
        static const std::vector<std::string> supported_extensions_;
    };

    // ============================================================
    // stream output operator (print all Action info)
    // ============================================================

    std::ostream& operator<<(std::ostream& os, const ActionLoader& loader);

} // namespace industrial_config_engine