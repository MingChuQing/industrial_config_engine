// include/industrial_config_engine/l3_group.hpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <memory>
#include <nlohmann/json.hpp>
#include "l2_node.hpp"
#include "types.hpp"

namespace industrial_config_engine {

    // ============================================================
    // 控制流模式枚举
    // ============================================================
    enum class GroupMode {
        SEQUENCE,   // 顺序执行
        PARALLEL,   // 并行执行
        LOOP,       // 循环执行
        IF,         // 条件分支
        SWITCH      // 多条件分支
    };

    // ============================================================
    // 循环类型枚举
    // ============================================================
    enum class LoopType {
        COUNT,      // 固定次数
        WHILE,      // 条件为真时循环
        UNTIL,      // 条件为真时停止
        FOREACH,    // 遍历列表
        DOWHILE     // 先执行后判断
    };

    // ============================================================
    // 循环配置
    // ============================================================
    struct LoopConfig {
        LoopType type = LoopType::COUNT;
        int count = 0;                  // COUNT: 循环次数
        std::string condition;          // WHILE/UNTIL: 条件表达式
        std::string items;              // FOREACH: 列表变量
        std::string item_name;          // FOREACH: 当前项变量名
        int max_iterations = 10000;     // 最大迭代次数（防止死循环）
    };

    // ============================================================
    // 条件配置
    // ============================================================
    struct ConditionConfig {
        std::string type;               // compare, expression, exists, choice, value
        std::string condition;          // compare 条件: eq, ne, lt, le, gt, ge
        std::string value;              // compare: 比较值; choice: 匹配值
        std::string source;             // value: 变量来源
        std::string expression;         // expression: 表达式
        std::string variable;           // exists: 变量名
    };

    // ============================================================
    // Case 分支（用于 switch）
    // ============================================================
    struct SwitchCase {
        std::string case_value;         // 匹配值（字符串形式）
        nlohmann::json body;            // 分支体
    };

    // ============================================================
    // 参数定义
    // ============================================================
    struct GroupArgDef {
        int index = 0;
        std::string name;
        DataType type = DataType::VOIDDataType;
        std::optional<std::string> default_value;
        std::string desc;
    };

    // ============================================================
    // L3 执行组
    // ============================================================
    class L3Group {
    public:
        L3Group() = default;
        explicit L3Group(const nlohmann::json& json);
        explicit L3Group(const std::string& filename, const nlohmann::json& json);
        explicit L3Group(const std::string& filename, const std::string& json_str);

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
        bool validateLoop() const;
        bool validateCondition() const;
        bool validateCases() const;
        bool validateMaxNodes() const;
        bool validateCircularReference(const std::vector<std::string>& ancestors = {}) const;

        // ============================================================
        // 节点计数
        // ============================================================

        int countNodes() const;

        // ============================================================
        // Getters
        // ============================================================

        const std::string& getFilename() const { return filename_; }
        const std::string& getName() const { return name_; }
        const std::string& getDescription() const { return description_; }
        GroupMode getMode() const { return mode_; }
        int getMaxNodes() const { return max_nodes_; }
        int getDelayBetween() const { return delay_between_; }
        bool getConfirmBetween() const { return confirm_between_; }
        const nlohmann::json& getBody() const { return body_; }
        const std::vector<GroupArgDef>& getArgs() const { return args_; }
        bool isInitialized() const { return initialized_; }

        // 循环相关
        bool hasLoop() const { return loop_.has_value(); }
        const std::optional<LoopConfig>& getLoop() const { return loop_; }

        // 条件相关
        bool hasCondition() const { return condition_.has_value(); }
        const std::optional<ConditionConfig>& getCondition() const { return condition_; }

        // 分支相关
        bool hasThen() const { return !then_body_.is_null(); }
        bool hasElse() const { return !else_body_.is_null(); }
        const nlohmann::json& getThen() const { return then_body_; }
        const nlohmann::json& getElse() const { return else_body_; }

        // Switch 相关
        bool hasCases() const { return !cases_.empty(); }
        bool hasDefault() const { return !default_body_.is_null(); }
        const std::vector<SwitchCase>& getCases() const { return cases_; }
        const nlohmann::json& getDefault() const { return default_body_; }

        // ============================================================
        // 参数解析
        // ============================================================

        std::vector<std::string> extractPlaceholders() const;
        std::string replacePlaceholders(const std::string& template_str,
            const std::unordered_map<std::string, std::string>& values) const;

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
        void parseArgs(const nlohmann::json& json);
        void parseBody(const nlohmann::json& json);
        void parseLoop(const nlohmann::json& json);
        void parseCondition(const nlohmann::json& json);
        void parseThenElse(const nlohmann::json& json);
        void parseSwitch(const nlohmann::json& json);

        // ============================================================
        // 辅助方法
        // ============================================================

        std::vector<std::string> extractPlaceholdersFromString(const std::string& str) const;
        std::string modeToString(GroupMode mode) const;
        GroupMode stringToMode(const std::string& mode_str) const;
        std::string loopTypeToString(LoopType type) const;
        LoopType stringToLoopType(const std::string& type_str) const;
        int countNodesInJson(const nlohmann::json& body) const;

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
        GroupMode mode_ = GroupMode::SEQUENCE;
        int max_nodes_ = 100;
        int delay_between_ = 0;
        bool confirm_between_ = false;

        // Body（包含 L2 Node 或 L3 Group 引用）
        nlohmann::json body_;

        // 参数定义
        std::vector<GroupArgDef> args_;

        // 循环配置（mode = LOOP 时使用）
        std::optional<LoopConfig> loop_;

        // 条件配置（mode = IF 时使用）
        std::optional<ConditionConfig> condition_;

        // 分支体（mode = IF 时使用）
        nlohmann::json then_body_;
        nlohmann::json else_body_;

        // Switch 分支（mode = SWITCH 时使用）
        std::vector<SwitchCase> cases_;
        nlohmann::json default_body_;

        // 是否已初始化
        bool initialized_ = false;
    };

    // ============================================================
    // L3 Group 加载器
    // ============================================================
    class L3GroupLoader {
    public:
        L3GroupLoader() = default;

        // 加载单个 Group 文件
        L3Group loadFromFile(const std::string& filepath);

        // 加载目录下所有 Group 文件
        std::vector<L3Group> loadFromDirectory(const std::string& dir_path);

        // 加载 Group Bundle（聚合文件）
        std::vector<L3Group> loadBundle(const std::string& bundle_path);

        // 解析引用路径
        L3Group resolveReference(const std::string& template_path,
            const std::vector<nlohmann::json>& params = {});

        // 设置 L2 Node 目录（用于解析 Node 引用）
        void setL2NodeDir(const std::string& dir) { l2_node_dir_ = dir; }

        // 设置 L3 Group 目录（用于解析 Group 引用）
        void setL3GroupDir(const std::string& dir) { l3_group_dir_ = dir; }

        // 设置缓存
        void setCache(std::shared_ptr<std::unordered_map<std::string, L3Group>> cache) {
            cache_ = cache;
        }

    private:
        std::string l2_node_dir_ = "L2_node";
        std::string l3_group_dir_ = "L3_group";
        std::shared_ptr<std::unordered_map<std::string, L3Group>> cache_;

        nlohmann::json loadJsonFile(const std::string& path);
        std::string resolvePath(const std::string& template_path);
    };

    // ============================================================
    // 流输出操作符
    // ============================================================
    std::ostream& operator<<(std::ostream& os, const L3Group& group);

} // namespace industrial_config_engine