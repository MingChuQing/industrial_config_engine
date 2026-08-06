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
    // 回调函数类型定义
    // ============================================================

    // 加载进度回调: (文件路径, 已加载数量, 总数量)
    using LoadProgressCallback = std::function<void(const std::string&, int, int)>;

    // 加载错误回调: (文件路径, 错误信息)
    using LoadErrorCallback = std::function<void(const std::string&, const std::string&)>;

    // Action加载完成回调: (key, Action指针)
    using ActionLoadedCallback = std::function<void(const std::string&, const L1Action&)>;

    // ============================================================
    // ActionLoader 类
    // ============================================================

    class ActionLoader {
    public:
        ActionLoader();
        ~ActionLoader();

        // ============================================================
        // 回调设置
        // ============================================================

        void setProgressCallback(LoadProgressCallback callback) { progress_callback_ = callback; }
        void setErrorCallback(LoadErrorCallback callback) { error_callback_ = callback; }
        void setActionLoadedCallback(ActionLoadedCallback callback) { action_loaded_callback_ = callback; }

        // ============================================================
        // 加载接口
        // ============================================================

        // 加载单个文件
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

        // 获取Action（const版本）
        const L1Action* getAction(const std::string& key) const;

        // 获取Action（非const版本）
        L1Action* getAction(const std::string& key);

        // 检查是否存在
        bool hasAction(const std::string& key) const;

        // 获取所有key（排序后）
        std::vector<std::string> getActionKeys() const;

        // 获取所有Action
        const std::unordered_map<std::string, L1Action>& getAllActions() const { return actions_; }

        // 按前缀查找
        std::vector<std::string> findActionsByPrefix(const std::string& prefix) const;

        // 按类型查找
        std::vector<std::string> findActionsByType(const std::string& type) const;

        // 按协议查找
        std::vector<std::string> findActionsByProtocol(const std::string& protocol) const;

        // ============================================================
        // 统计信息
        // ============================================================

        size_t getActionCount() const { return actions_.size(); }
        size_t getLoadedFileCount() const { return loaded_files_; }
        size_t getErrorCount() const { return error_count_; }

        // 获取类型统计
        std::unordered_map<std::string, int> getTypeStatistics() const;

        // 获取协议统计
        std::unordered_map<std::string, int> getProtocolStatistics() const;

        // ============================================================
        // 管理接口
        // ============================================================

        // 清空所有Action
        void clear();

        // 移除指定Action
        bool removeAction(const std::string& key);

        // 设置严格模式（是否严格校验）
        void setStrictMode(bool strict) { strict_mode_ = strict; }
        bool getStrictMode() const { return strict_mode_; }

        // 设置是否允许覆盖
        void setAllowOverwrite(bool allow) { allow_overwrite_ = allow; }
        bool getAllowOverwrite() const { return allow_overwrite_; }

    private:
        // ============================================================
        // 内部加载方法
        // ============================================================

        // 加载JSON文件（自动识别单个或聚合）
        bool loadJsonFile(const std::string& filepath, const std::string& base_path);

        // 加载单个Action
        bool loadSingleActionFromJson(const nlohmann::json& json,
            const std::string& base_path,
            const std::string& filename);

        // 加载聚合文件（action_bundle）
        bool loadBundleFromJson(const nlohmann::json& json,
            const std::string& base_path,
            const std::string& bundle_filename);

        // ============================================================
        // 辅助方法
        // ============================================================

        // 生成唯一key
        std::string makeKey(const std::string& base_path,
            const std::string& bundle_name,
            const std::string& filename) const;

        // 规范化路径
        std::string normalizePath(const std::string& path) const;

        // 判断是否为聚合文件
        bool isBundleFile(const nlohmann::json& json) const;

        // 递归遍历目录
        void traverseDirectory(const std::string& directory_path,
            const std::string& base_path,
            std::vector<std::string>& files);

        // 提取文件名（不含扩展名）
        std::string getBaseName(const std::string& filename) const;

        // 检查文件扩展名是否支持
        bool isSupportedExtension(const std::string& ext) const;

    private:
        // ============================================================
        // 成员变量
        // ============================================================

        // Action存储: key -> L1Action
        std::unordered_map<std::string, L1Action> actions_;

        // 统计信息
        size_t loaded_files_ = 0;
        size_t error_count_ = 0;

        // 配置
        bool strict_mode_ = true;
        bool allow_overwrite_ = false;

        // 回调
        LoadProgressCallback progress_callback_;
        LoadErrorCallback error_callback_;
        ActionLoadedCallback action_loaded_callback_;

        // 支持的扩展名
        static const std::vector<std::string> supported_extensions_;
    };

    // ============================================================
    // 流输出操作符（打印所有Action信息）
    // ============================================================

    std::ostream& operator<<(std::ostream& os, const ActionLoader& loader);

} // namespace industrial_config_engine