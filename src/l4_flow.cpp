#include "industrial_config_engine/l4_flow.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <regex>
#include <filesystem>

namespace industrial_config_engine {

    namespace fs = std::filesystem;

    // ============================================================
    // L4Flow constructor and load
    // ============================================================

    L4Flow::L4Flow(const nlohmann::json& json) {
        loadFromJson(json);
    }

    L4Flow::L4Flow(const std::string& filename, const nlohmann::json& json)
        : filename_(filename) {
        loadFromJson(json);
    }

    L4Flow::L4Flow(const std::string& filename, const std::string& json_str)
        : filename_(filename) {
        loadFromJsonString(json_str);
    }

    bool L4Flow::loadFromJson(const nlohmann::json& json) {
        try {
            parseCommonFields(json);
            parseProfiles(json);
            parseBody(json);
            parseOnTimeout(json);
            parseEmergencyCleanup(json);

            initialized_ = true;
            return validate();
        }
        catch (const std::exception& e) {
            initialized_ = false;
            return false;
        }
    }

    bool L4Flow::loadFromJsonString(const std::string& json_str) {
        try {
            nlohmann::json json = nlohmann::json::parse(json_str);
            return loadFromJson(json);
        }
        catch (const std::exception& e) {
            initialized_ = false;
            return false;
        }
    }

    bool L4Flow::loadFromFile(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return false;
        }

        nlohmann::json json;
        try {
            file >> json;
        }
        catch (const std::exception& e) {
            return false;
        }

        filepath_ = filepath;

        // extract filename from path
        size_t pos = filepath.find_last_of("/\\");
        std::string basename = (pos != std::string::npos) ? filepath.substr(pos + 1) : filepath;
        pos = basename.find_last_of('.');
        filename_ = (pos != std::string::npos) ? basename.substr(0, pos) : basename;

        return loadFromJson(json);
    }

    // ============================================================
    // export to JSON
    // ============================================================

    nlohmann::json L4Flow::toJson() const {
        nlohmann::json json;

        json["type"] = "flow";
        json["name"] = name_;

        if (!description_.empty()) {
            json["description"] = description_;
        }

        if (version_ != "1.0.0") {
            json["version"] = version_;
        }

        json["timeout_ms"] = timeout_ms_;

        // Profiles
        if (!profiles_.empty()) {
            nlohmann::json profiles_json;
            for (const auto& [name, profile] : profiles_) {
                nlohmann::json profile_json;
                if (!profile.description.empty()) {
                    profile_json["description"] = profile.description;
                }
                nlohmann::json params_json;
                for (const auto& [key, value] : profile.params) {
                    params_json[key] = value;
                }
                profile_json["params"] = params_json;
                profiles_json[name] = profile_json;
            }
            json["profiles"] = profiles_json;
        }

        if (!active_profile_.empty()) {
            json["active_profile"] = active_profile_;
        }

        if (!body_.is_null()) {
            json["body"] = body_;
        }

        if (!on_timeout_.is_null() && on_timeout_.is_array() && !on_timeout_.empty()) {
            json["on_timeout"] = on_timeout_;
        }

        if (emergency_cleanup_.has_value()) {
            nlohmann::json cleanup_json;
            cleanup_json["type"] = emergency_cleanup_->type;
            cleanup_json["body"] = emergency_cleanup_->body;
            if (!emergency_cleanup_->inherit_parent.empty()) {
                cleanup_json["inherit_parent"] = emergency_cleanup_->inherit_parent;
            }
            json["on_emergency_cleanup"] = cleanup_json;
        }

        return json;
    }

    std::string L4Flow::toJsonString(bool pretty) const {
        nlohmann::json json = toJson();
        return pretty ? json.dump(4) : json.dump();
    }

    // ============================================================
    // validation
    // ============================================================

    bool L4Flow::validate() const {
        if (name_.empty()) return false;
        if (body_.is_null() || !body_.is_array() || body_.empty()) return false;
        if (!validateBody()) return false;
        if (!validateProfiles()) return false;
        if (!validateActiveProfile()) return false;
        if (!validateEmergencyCleanup()) return false;

        // if on_timeout without timeout setting, warn but do not error
        if (hasOnTimeout() && !hasTimeout()) {
            // may print warning, but do not block load
        }

        return true;
    }

    bool L4Flow::validateBody() const {
        if (body_.is_null() || !body_.is_array()) return false;

        for (const auto& item : body_) {
            if (!item.is_object()) return false;
            if (!item.contains("type")) return false;
            std::string type = item["type"].get<std::string>();
            if (type != "group") return false;
        }

        return true;
    }

    bool L4Flow::validateProfiles() const {
        // Profiles are optional
        return true;
    }

    bool L4Flow::validateActiveProfile() const {
        if (active_profile_.empty()) return true;
        return profiles_.find(active_profile_) != profiles_.end();
    }

    bool L4Flow::validateEmergencyCleanup() const {
        if (!emergency_cleanup_.has_value()) return true;

        const auto& cleanup = emergency_cleanup_.value();
        if (cleanup.type.empty()) return false;
        if (cleanup.type != "sequence" && cleanup.type != "parallel") return false;
        if (cleanup.body.is_null() || !cleanup.body.is_array() || cleanup.body.empty()) return false;

        return true;
    }

    // ============================================================
    // Profile operations
    // ============================================================

    bool L4Flow::hasProfile(const std::string& name) const {
        return profiles_.find(name) != profiles_.end();
    }

    const ProfileConfig* L4Flow::getProfile(const std::string& name) const {
        auto it = profiles_.find(name);
        if (it != profiles_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    bool L4Flow::setActiveProfile(const std::string& name) {
        if (profiles_.find(name) != profiles_.end()) {
            active_profile_ = name;
            return true;
        }
        return false;
    }

    std::unordered_map<std::string, std::string> L4Flow::getCurrentProfileParams() const {
        if (active_profile_.empty()) {
            return {};
        }
        auto it = profiles_.find(active_profile_);
        if (it != profiles_.end()) {
            return it->second.params;
        }
        return {};
    }

    // ============================================================
    // placeholder handling
    // ============================================================

    std::vector<std::string> L4Flow::extractPlaceholders() const {
        std::vector<std::string> result;

        auto addPlaceholders = [&](const std::string& str) {
            auto extracted = extractPlaceholdersFromString(str);
            result.insert(result.end(), extracted.begin(), extracted.end());
            };

        // extract from name, description
        addPlaceholders(name_);
        addPlaceholders(description_);

        // extract from version
        addPlaceholders(version_);

        // extract from body (JSON string values)
        if (!body_.is_null()) {
            std::string body_str = body_.dump();
            addPlaceholders(body_str);
        }

        // extract from on_timeout
        if (!on_timeout_.is_null()) {
            std::string on_timeout_str = on_timeout_.dump();
            addPlaceholders(on_timeout_str);
        }

        // extract from profile params
        for (const auto& [profile_name, profile] : profiles_) {
            addPlaceholders(profile_name);
            addPlaceholders(profile.description);
            for (const auto& [key, value] : profile.params) {
                addPlaceholders(key);
                addPlaceholders(value);
            }
        }

        // extract from active_profile
        addPlaceholders(active_profile_);

        // extract from emergency_cleanup
        if (emergency_cleanup_.has_value()) {
            addPlaceholders(emergency_cleanup_->type);
            if (!emergency_cleanup_->body.is_null()) {
                addPlaceholders(emergency_cleanup_->body.dump());
            }
            addPlaceholders(emergency_cleanup_->inherit_parent);
        }

        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());

        return result;
    }

    std::string L4Flow::replacePlaceholders(const std::string& template_str,
        const std::unordered_map<std::string, std::string>& values) const {
        std::string result = template_str;
        static const std::regex pattern(R"(\$\{([^}]+)\})");
        std::smatch match;
        std::string::const_iterator search_start(result.cbegin());

        while (std::regex_search(search_start, result.cend(), match, pattern)) {
            std::string placeholder = match[1].str();
            auto it = values.find(placeholder);
            if (it != values.end()) {
                std::string replacement = it->second;
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

    std::vector<std::string> L4Flow::extractPlaceholdersFromString(const std::string& str) const {
        std::vector<std::string> result;
        static const std::regex pattern(R"(\$\{([^}]+)\})");
        std::smatch match;
        std::string::const_iterator search_start(str.cbegin());

        while (std::regex_search(search_start, str.cend(), match, pattern)) {
            result.push_back(match[1].str());
            search_start = match.suffix().first;
        }

        return result;
    }

    std::string L4Flow::valueToString(const nlohmann::json& value) const {
        if (value.is_string()) {
            return value.get<std::string>();
        }
        else if (value.is_number()) {
            return std::to_string(value.get<double>());
        }
        else if (value.is_boolean()) {
            return value.get<bool>() ? "true" : "false";
        }
        else if (value.is_null()) {
            return "null";
        }
        else {
            return value.dump();
        }
    }

    // ============================================================
    // private parse methods
    // ============================================================

    void L4Flow::parseCommonFields(const nlohmann::json& json) {
        if (json.contains("name") && json["name"].is_string()) {
            name_ = json["name"].get<std::string>();
        }

        if (json.contains("description") && json["description"].is_string()) {
            description_ = json["description"].get<std::string>();
        }

        if (json.contains("version") && json["version"].is_string()) {
            version_ = json["version"].get<std::string>();
        }

        if (json.contains("timeout_ms") && json["timeout_ms"].is_number()) {
            timeout_ms_ = json["timeout_ms"].get<uint32_t>();
        }

        if (json.contains("active_profile") && json["active_profile"].is_string()) {
            active_profile_ = json["active_profile"].get<std::string>();
        }
    }

    void L4Flow::parseProfiles(const nlohmann::json& json) {
        profiles_.clear();

        if (!json.contains("profiles") || !json["profiles"].is_object()) {
            return;
        }

        for (auto it = json["profiles"].begin(); it != json["profiles"].end(); ++it) {
            const std::string& name = it.key();
            const auto& profile_json = it.value();

            ProfileConfig profile;

            if (profile_json.contains("description") && profile_json["description"].is_string()) {
                profile.description = profile_json["description"].get<std::string>();
            }

            if (profile_json.contains("params") && profile_json["params"].is_object()) {
                for (auto param_it = profile_json["params"].begin();
                    param_it != profile_json["params"].end(); ++param_it) {
                    profile.params[param_it.key()] = valueToString(param_it.value());
                }
            }

            profiles_[name] = profile;
        }
    }

    void L4Flow::parseBody(const nlohmann::json& json) {
        if (json.contains("body") && json["body"].is_array()) {
            body_ = json["body"];
        }
        else {
            body_ = nlohmann::json::array();
        }
    }

    void L4Flow::parseOnTimeout(const nlohmann::json& json) {
        if (json.contains("on_timeout") && json["on_timeout"].is_array()) {
            on_timeout_ = json["on_timeout"];
        }
        else {
            on_timeout_ = nlohmann::json::array();
        }
    }

    void L4Flow::parseEmergencyCleanup(const nlohmann::json& json) {
        emergency_cleanup_.reset();

        if (!json.contains("on_emergency_cleanup") || !json["on_emergency_cleanup"].is_object()) {
            return;
        }

        const auto& cleanup_json = json["on_emergency_cleanup"];
        EmergencyCleanupConfig cleanup;

        if (cleanup_json.contains("type") && cleanup_json["type"].is_string()) {
            cleanup.type = cleanup_json["type"].get<std::string>();
        }

        if (cleanup_json.contains("body") && cleanup_json["body"].is_array()) {
            cleanup.body = cleanup_json["body"];
        }

        if (cleanup_json.contains("inherit_parent") && cleanup_json["inherit_parent"].is_string()) {
            cleanup.inherit_parent = cleanup_json["inherit_parent"].get<std::string>();
        }

        emergency_cleanup_ = cleanup;
    }

    // ============================================================
    // debug and print
    // ============================================================

    std::string L4Flow::toString() const {
        std::ostringstream oss;
        print(oss);
        return oss.str();
    }

    void L4Flow::print(std::ostream& os) const {
        os << "========================================" << std::endl;
        os << "L4 Flow Information" << std::endl;
        os << "========================================" << std::endl;

        os << "Filename: " << filename_ << std::endl;
        os << "Name: " << name_ << std::endl;
        if (!description_.empty()) {
            os << "Description: " << description_ << std::endl;
        }
        os << "Version: " << version_ << std::endl;
        os << "Timeout: " << timeout_ms_ << " ms" << std::endl;

        // Profiles
        if (!profiles_.empty()) {
            os << "Profiles:" << std::endl;
            for (const auto& [name, profile] : profiles_) {
                os << "  [" << name << "]";
                if (!profile.description.empty()) {
                    os << " " << profile.description;
                }
                os << std::endl;
                if (!profile.params.empty()) {
                    os << "    Params:" << std::endl;
                    for (const auto& [key, value] : profile.params) {
                        os << "      " << key << " = " << value << std::endl;
                    }
                }
            }
        }

        if (!active_profile_.empty()) {
            os << "Active Profile: " << active_profile_ << std::endl;
        }

        // Body
        if (!body_.is_null() && body_.is_array()) {
            os << "Body: " << body_.size() << " items" << std::endl;
        }

        // on_timeout
        if (!on_timeout_.is_null() && on_timeout_.is_array() && !on_timeout_.empty()) {
            os << "On Timeout: " << on_timeout_.size() << " items" << std::endl;
        }

        // Emergency Cleanup
        if (emergency_cleanup_.has_value()) {
            os << "Emergency Cleanup:" << std::endl;
            os << "  Type: " << emergency_cleanup_->type << std::endl;
            if (!emergency_cleanup_->body.is_null() && emergency_cleanup_->body.is_array()) {
                os << "  Body: " << emergency_cleanup_->body.size() << " items" << std::endl;
            }
            if (!emergency_cleanup_->inherit_parent.empty()) {
                os << "  Inherit Parent: " << emergency_cleanup_->inherit_parent << std::endl;
            }
        }

        // placeholder
        auto placeholders = extractPlaceholders();
        if (!placeholders.empty()) {
            os << "Placeholders: ";
            for (const auto& p : placeholders) {
                os << "${" << p << "} ";
            }
            os << std::endl;
        }

        os << "========================================" << std::endl;
    }

    std::ostream& operator<<(std::ostream& os, const L4Flow& flow) {
        flow.print(os);
        return os;
    }

    // ============================================================
    // L4FlowLoader implementation
    // ============================================================

    L4Flow L4FlowLoader::loadFromFile(const std::string& filepath) {
        L4Flow flow;
        flow.loadFromFile(filepath);
        return flow;
    }

    std::vector<L4Flow> L4FlowLoader::loadFromDirectory(const std::string& dir_path) {
        std::vector<L4Flow> flows;

        try {
            for (const auto& entry : fs::recursive_directory_iterator(dir_path)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    std::string path = entry.path().string();
                    // check if flow file (contains _flow)
                    if (path.find("_flow") != std::string::npos) {
                        L4Flow flow = loadFromFile(path);
                        if (flow.isInitialized()) {
                            flows.push_back(flow);
                            if (cache_) {
                                (*cache_)[path] = flow;
                            }
                        }
                    }
                }
            }
        }
        catch (const std::exception& e) {
            // ignore directory scan errors
        }

        return flows;
    }

    std::vector<L4Flow> L4FlowLoader::loadBundle(const std::string& bundle_path) {
        std::vector<L4Flow> flows;
        nlohmann::json root = loadJsonFile(bundle_path);

        if (!root.contains("type") || root["type"].get<std::string>() != "flow_bundle") {
            return flows;
        }

        if (!root.contains("flows") || !root["flows"].is_array()) {
            return flows;
        }

        for (const auto& flow_json : root["flows"]) {
            if (flow_json.contains("filename") && flow_json["filename"].is_string()) {
                std::string filename = flow_json["filename"].get<std::string>();
                L4Flow flow(filename, flow_json);
                if (flow.isInitialized()) {
                    flows.push_back(flow);
                }
            }
        }

        return flows;
    }

    nlohmann::json L4FlowLoader::loadJsonFile(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + path);
        }
        nlohmann::json root;
        file >> root;
        return root;
    }

    std::string L4FlowLoader::resolvePath(const std::string& template_path) {
        // if already absolute or relative path, return directly
        if (template_path.find("/") != std::string::npos ||
            template_path.find("\\") != std::string::npos) {
            return template_path;
        }

        // try to find under L3_group directory
        return l3_group_dir_ + "/" + template_path + ".group.json";
    }

} // namespace industrial_config_engine