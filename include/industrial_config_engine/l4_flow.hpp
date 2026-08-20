#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <memory>
#include <nlohmann/json.hpp>
#include "l3_group.hpp"
#include "types.hpp"

namespace industrial_config_engine {

    // ============================================================
    // Profile config
    // ============================================================
    struct ProfileConfig {
        std::string description;
        std::unordered_map<std::string, std::string> params;  // param name -> param value (string form)
    };

    // ============================================================
    // emergency cleanup config
    // ============================================================
    struct EmergencyCleanupConfig {
        std::string type;           // sequence / parallel
        nlohmann::json body;        // execution body
        std::string inherit_parent; // skip / inherit
    };

    // ============================================================
    // L4 Flow class
    // ============================================================
    class L4Flow {
    public:
        L4Flow() = default;
        explicit L4Flow(const nlohmann::json& json);
        explicit L4Flow(const std::string& filename, const nlohmann::json& json);
        explicit L4Flow(const std::string& filename, const std::string& json_str);

        // ============================================================
        // load and save
        // ============================================================

        bool loadFromJson(const nlohmann::json& json);
        bool loadFromJsonString(const std::string& json_str);
        bool loadFromFile(const std::string& filepath);

        nlohmann::json toJson() const;
        std::string toJsonString(bool pretty = true) const;

        // ============================================================
        // validation
        // ============================================================

        bool validate() const;
        bool validateBody() const;
        bool validateProfiles() const;
        bool validateActiveProfile() const;
        bool validateEmergencyCleanup() const;

        // ============================================================
        // parameter parsing
        // ============================================================

        std::vector<std::string> extractPlaceholders() const;
        std::string replacePlaceholders(const std::string& template_str,
            const std::unordered_map<std::string, std::string>& values) const;

        // ============================================================
        // Profile operations
        // ============================================================

        bool hasProfile(const std::string& name) const;
        const ProfileConfig* getProfile(const std::string& name) const;
        const std::unordered_map<std::string, ProfileConfig>& getProfiles() const { return profiles_; }
        const std::string& getActiveProfile() const { return active_profile_; }
        bool setActiveProfile(const std::string& name);

        // get current profile params
        std::unordered_map<std::string, std::string> getCurrentProfileParams() const;

        // ============================================================
        // Getters
        // ============================================================

        const std::string& getFilename() const { return filename_; }
        const std::string& getName() const { return name_; }
        const std::string& getDescription() const { return description_; }
        const std::string& getVersion() const { return version_; }
        uint32_t getTimeoutMs() const { return timeout_ms_; }
        const nlohmann::json& getBody() const { return body_; }
        const nlohmann::json& getOnTimeout() const { return on_timeout_; }
        const std::optional<EmergencyCleanupConfig>& getEmergencyCleanup() const { return emergency_cleanup_; }
        bool isInitialized() const { return initialized_; }

        // ============================================================
        // convenience checks
        // ============================================================

        bool hasTimeout() const { return timeout_ms_ > 0; }
        bool hasOnTimeout() const { return !on_timeout_.is_null() && on_timeout_.is_array() && !on_timeout_.empty(); }
        bool hasEmergencyCleanup() const { return emergency_cleanup_.has_value(); }
        bool hasProfiles() const { return !profiles_.empty(); }
        bool hasDescription() const { return !description_.empty(); }

        // ============================================================
        // debug and print
        // ============================================================

        std::string toString() const;
        void print(std::ostream& os = std::cout) const;

    private:
        // ============================================================
        // private parse methods
        // ============================================================

        void parseCommonFields(const nlohmann::json& json);
        void parseProfiles(const nlohmann::json& json);
        void parseBody(const nlohmann::json& json);
        void parseOnTimeout(const nlohmann::json& json);
        void parseEmergencyCleanup(const nlohmann::json& json);

        // ============================================================
        // helper methods
        // ============================================================

        std::vector<std::string> extractPlaceholdersFromString(const std::string& str) const;
        std::string valueToString(const nlohmann::json& value) const;

    private:
        // ============================================================
        // member variables
        // ============================================================

        // file info
        std::string filename_;
        std::string filepath_;

        // core fields
        std::string name_;
        std::string description_;
        std::string version_ = "1.0.0";
        uint32_t timeout_ms_ = 3600000;  // default 1 hour

        // Profiles
        std::unordered_map<std::string, ProfileConfig> profiles_;
        std::string active_profile_;

        // Body (L3 Group references or inline definitions)
        nlohmann::json body_;

        // on_timeout branch
        nlohmann::json on_timeout_;

        // emergency cleanup config
        std::optional<EmergencyCleanupConfig> emergency_cleanup_;

        // whether initialized
        bool initialized_ = false;
    };

    // ============================================================
    // L4 Flow loader
    // ============================================================
    class L4FlowLoader {
    public:
        L4FlowLoader() = default;

        // load single Flow file
        L4Flow loadFromFile(const std::string& filepath);

        // load all Flow files in directory
        std::vector<L4Flow> loadFromDirectory(const std::string& dir_path);

        // load Flow bundle (aggregate file)
        std::vector<L4Flow> loadBundle(const std::string& bundle_path);

        // set L3 Group directory (for resolving Group references)
        void setL3GroupDir(const std::string& dir) { l3_group_dir_ = dir; }

        // set cache
        void setCache(std::shared_ptr<std::unordered_map<std::string, L4Flow>> cache) {
            cache_ = cache;
        }

    private:
        std::string l3_group_dir_ = "L3_group";
        std::shared_ptr<std::unordered_map<std::string, L4Flow>> cache_;

        nlohmann::json loadJsonFile(const std::string& path);
        std::string resolvePath(const std::string& template_path);
    };

    // ============================================================
    // stream output operator
    // ============================================================
    std::ostream& operator<<(std::ostream& os, const L4Flow& flow);

} // namespace industrial_config_engine