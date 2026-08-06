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
    // Profile 配置
    // ============================================================
    struct ProfileConfig {
        std::string description;
        std::unordered_map<std::string, std::string> params;  // 参数名 -> 参数值（字符串形式）
    };

    // ============================================================
    // 紧急清理配置
    // ============================================================
    struct EmergencyCleanupConfig {
        std::string type;           // sequence / parallel
        nlohmann::json body;        // 执行体
        std::string inherit_parent; // skip / inherit
    };

    // ============================================================
    // L4 Flow 类
    // ============================================================
    class L4Flow {
    public:
        L4Flow() = default;
        explicit L4Flow(const nlohmann::json& json);
        explicit L4Flow(const std::string& filename, const nlohmann::json& json);
        explicit L4Flow(const std::string& filename, const std::string& json_str);

        // ============================================================
        // 加载和保存
        // ============================================================

        bool loadFromJson(const nlohmann::json& json);
        bool loadFromJsonString(const std::string& json_str);
        bool loadFromFile(const std::string& filepath);

        nlohmann::json toJson() const;
        std::string toJsonString(bool pretty = true) const;

        // ============================================================
        // 验证
        // ============================================================

        bool validate() const;
        bool validateBody() const;
        bool validateProfiles() const;
        bool validateActiveProfile() const;
        bool validateEmergencyCleanup() const;

        // ============================================================
        // 参数解析
        // ============================================================

        std::vector<std::string> extractPlaceholders() const;
        std::string replacePlaceholders(const std::string& template_str,
            const std::unordered_map<std::string, std::string>& values) const;

        // ============================================================
        // Profile 操作
        // ============================================================

        bool hasProfile(const std::string& name) const;
        const ProfileConfig* getProfile(const std::string& name) const;
        const std::unordered_map<std::string, ProfileConfig>& getProfiles() const { return profiles_; }
        const std::string& getActiveProfile() const { return active_profile_; }
        bool setActiveProfile(const std::string& name);

        // 获取当前 profile 的参数
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
        // 便捷判断
        // ============================================================

        bool hasTimeout() const { return timeout_ms_ > 0; }
        bool hasOnTimeout() const { return !on_timeout_.is_null() && on_timeout_.is_array() && !on_timeout_.empty(); }
        bool hasEmergencyCleanup() const { return emergency_cleanup_.has_value(); }
        bool hasProfiles() const { return !profiles_.empty(); }
        bool hasDescription() const { return !description_.empty(); }

        // ============================================================
        // 调试和打印
        // ============================================================

        std::string toString() const;
        void print(std::ostream& os = std::cout) const;

    private:
        // ============================================================
        // 私有解析方法
        // ============================================================

        void parseCommonFields(const nlohmann::json& json);
        void parseProfiles(const nlohmann::json& json);
        void parseBody(const nlohmann::json& json);
        void parseOnTimeout(const nlohmann::json& json);
        void parseEmergencyCleanup(const nlohmann::json& json);

        // ============================================================
        // 辅助方法
        // ============================================================

        std::vector<std::string> extractPlaceholdersFromString(const std::string& str) const;
        std::string valueToString(const nlohmann::json& value) const;

    private:
        // ============================================================
        // 成员变量
        // ============================================================

        // 文件信息
        std::string filename_;
        std::string filepath_;

        // 核心字段
        std::string name_;
        std::string description_;
        std::string version_ = "1.0.0";
        uint32_t timeout_ms_ = 3600000;  // 默认 1 小时

        // Profiles
        std::unordered_map<std::string, ProfileConfig> profiles_;
        std::string active_profile_;

        // Body（包含 L3 Group 引用或内联定义）
        nlohmann::json body_;

        // on_timeout 分支
        nlohmann::json on_timeout_;

        // 紧急清理配置
        std::optional<EmergencyCleanupConfig> emergency_cleanup_;

        // 是否已初始化
        bool initialized_ = false;
    };

    // ============================================================
    // L4 Flow 加载器
    // ============================================================
    class L4FlowLoader {
    public:
        L4FlowLoader() = default;

        // 加载单个 Flow 文件
        L4Flow loadFromFile(const std::string& filepath);

        // 加载目录下所有 Flow 文件
        std::vector<L4Flow> loadFromDirectory(const std::string& dir_path);

        // 加载 Flow Bundle（聚合文件）
        std::vector<L4Flow> loadBundle(const std::string& bundle_path);

        // 设置 L3 Group 目录（用于解析 Group 引用）
        void setL3GroupDir(const std::string& dir) { l3_group_dir_ = dir; }

        // 设置缓存
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
    // 流输出操作符
    // ============================================================
    std::ostream& operator<<(std::ostream& os, const L4Flow& flow);

} // namespace industrial_config_engine