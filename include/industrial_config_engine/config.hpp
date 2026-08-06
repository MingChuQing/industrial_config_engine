// include/industrial_config_engine/config.hpp
#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace industrial_config_engine {

    // ============================================================
    // 配置结构体
    // ============================================================
    struct Config {
        // 支持的JSON文件扩展名
        std::vector<std::string> supported_extensions = { ".json", ".JSON" };

        // 是否启用严格模式（严格校验）
        bool strict_mode = true;

        // 是否允许覆盖已有的Action
        bool allow_overwrite = false;

        // 日志级别
        enum class LogLevel {
            DEBUG,
            INFO,
            WARNING,
            ERROR
        };
        LogLevel log_level = LogLevel::INFO;

        // ============================================================
        // 方法声明（在 config.cpp 中实现）
        // ============================================================

        // 检查扩展名是否支持
        bool isExtensionSupported(const std::string& extension) const;

        // 获取日志级别字符串
        std::string getLogLevelString() const;

        // 从文件加载配置
        bool loadFromFile(const std::string& filepath);

        // 导出为JSON
        nlohmann::json toJson() const;

        // 从JSON加载
        bool loadFromJson(const nlohmann::json& json);
    };

    // ============================================================
    // 全局配置单例类
    // ============================================================
    class GlobalConfig {
    public:
        // 获取单例实例
        static GlobalConfig& getInstance();

        // 设置配置
        void setConfig(const Config& config);

        // 获取配置
        const Config& getConfig() const;

        // 从文件加载配置
        bool loadFromFile(const std::string& filepath);

        // 重置为默认配置
        void reset();

    private:
        // 私有构造函数（单例模式）
        GlobalConfig() = default;
        ~GlobalConfig() = default;

        // 禁止拷贝和赋值
        GlobalConfig(const GlobalConfig&) = delete;
        GlobalConfig& operator=(const GlobalConfig&) = delete;

        // 配置数据
        Config config_;
    };

} // namespace industrial_config_engine