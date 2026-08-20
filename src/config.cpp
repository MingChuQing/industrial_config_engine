// src/config.cpp
#include "industrial_config_engine/config.hpp"
#include <fstream>
#include <iostream>

namespace industrial_config_engine {

    // ============================================================
    // Config method implementations
    // ============================================================

    bool Config::isExtensionSupported(const std::string& extension) const {
        for (const auto& ext : supported_extensions) {
            if (ext == extension) {
                return true;
            }
        }
        return false;
    }

    std::string Config::getLogLevelString() const {
        switch (log_level) {
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR:   return "ERROR";
        default:                return "UNKNOWN";
        }
    }

    bool Config::loadFromFile(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return false;
        }

        nlohmann::json json;
        try {
            file >> json;
        }
        catch (const std::exception& e) {
            std::cerr << "Failed to load config file: " << e.what() << std::endl;
            return false;
        }

        return loadFromJson(json);
    }

    bool Config::loadFromJson(const nlohmann::json& json) {
        try {
            if (json.contains("strict_mode") && json["strict_mode"].is_boolean()) {
                strict_mode = json["strict_mode"].get<bool>();
            }

            if (json.contains("allow_overwrite") && json["allow_overwrite"].is_boolean()) {
                allow_overwrite = json["allow_overwrite"].get<bool>();
            }

            if (json.contains("log_level") && json["log_level"].is_string()) {
                std::string level = json["log_level"].get<std::string>();
                if (level == "DEBUG") log_level = LogLevel::DEBUG;
                else if (level == "INFO") log_level = LogLevel::INFO;
                else if (level == "WARNING") log_level = LogLevel::WARNING;
                else if (level == "ERROR") log_level = LogLevel::ERROR;
            }

            if (json.contains("supported_extensions") && json["supported_extensions"].is_array()) {
                supported_extensions.clear();
                for (const auto& ext : json["supported_extensions"]) {
                    if (ext.is_string()) {
                        supported_extensions.push_back(ext.get<std::string>());
                    }
                }
            }

            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "Failed to parse config JSON: " << e.what() << std::endl;
            return false;
        }
    }

    nlohmann::json Config::toJson() const {
        nlohmann::json json;
        json["strict_mode"] = strict_mode;
        json["allow_overwrite"] = allow_overwrite;
        json["log_level"] = getLogLevelString();
        json["supported_extensions"] = supported_extensions;
        return json;
    }

    // ============================================================
    // GlobalConfig method implementations
    // ============================================================

    GlobalConfig& GlobalConfig::getInstance() {
        static GlobalConfig instance;
        return instance;
    }

    void GlobalConfig::setConfig(const Config& config) {
        config_ = config;
    }

    const Config& GlobalConfig::getConfig() const {
        return config_;
    }

    bool GlobalConfig::loadFromFile(const std::string& filepath) {
        return config_.loadFromFile(filepath);
    }

    void GlobalConfig::reset() {
        config_ = Config();  // reset to default config
    }

} // namespace industrial_config_engine