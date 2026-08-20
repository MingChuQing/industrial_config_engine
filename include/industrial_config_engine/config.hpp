// include/industrial_config_engine/config.hpp
#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace industrial_config_engine {

    // ============================================================
    // Configuration struct
    // ============================================================
    struct Config {
        // Supported JSON file extensions
        std::vector<std::string> supported_extensions = { ".json", ".JSON" };

        // Whether strict mode is enabled (strict validation)
        bool strict_mode = true;

        // Whether overwriting existing Actions is allowed
        bool allow_overwrite = false;

        // Log level
        enum class LogLevel {
            DEBUG,
            INFO,
            WARNING,
            ERROR
        };
        LogLevel log_level = LogLevel::INFO;

        // ============================================================
        // Method declarations (implemented in config.cpp)
        // ============================================================

        // Check whether the extension is supported
        bool isExtensionSupported(const std::string& extension) const;

        // Get the log level string
        std::string getLogLevelString() const;

        // Load configuration from file
        bool loadFromFile(const std::string& filepath);

        // Export to JSON
        nlohmann::json toJson() const;

        // Load from JSON
        bool loadFromJson(const nlohmann::json& json);
    };

    // ============================================================
    // Global configuration singleton class
    // ============================================================
    class GlobalConfig {
    public:
        // Get the singleton instance
        static GlobalConfig& getInstance();

        // Set the configuration
        void setConfig(const Config& config);

        // Get the configuration
        const Config& getConfig() const;

        // Load configuration from file
        bool loadFromFile(const std::string& filepath);

        // Reset to default configuration
        void reset();

    private:
        // Private constructor (singleton pattern)
        GlobalConfig() = default;
        ~GlobalConfig() = default;

        // Disable copy and assignment
        GlobalConfig(const GlobalConfig&) = delete;
        GlobalConfig& operator=(const GlobalConfig&) = delete;

        // Configuration data
        Config config_;
    };

} // namespace industrial_config_engine