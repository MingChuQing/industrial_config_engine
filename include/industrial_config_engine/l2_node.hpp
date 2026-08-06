// include/industrial_config_engine/l2_node.hpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <memory>
#include <nlohmann/json.hpp>
#include "l1_action.hpp"
#include "types.hpp"

namespace industrial_config_engine {

    // ============================================================
    // Judge 类型枚举
    // ============================================================
    enum class JudgeType {
        NONE,       // 无判断，默认成功
        COMPARE,    // 值比较
        EXPRESSION, // 表达式判断
        EXISTS      // 存在性判断
    };

    // ============================================================
    // Judge 配置
    // ============================================================
    struct JudgeConfig {
        JudgeType type = JudgeType::NONE;
        std::string condition;      // compare: eq, ne, lt, le, gt, ge, abs_ge, abs_gt, abs_le, abs_lt
        std::string value;          // compare: 比较值; expression: 表达式
        std::string expression;     // expression: 表达式字符串

        // 判断是否为有效配置
        bool isValid() const;

        // 获取占位符
        std::vector<std::string> getPlaceholders() const;
    };

    // ============================================================
    // Action 引用（L1 原子操作引用）
    // ============================================================
    struct ActionRef {
        std::string template_path;   // L1 Action 文件路径（template 引用）
        std::string inline_json;     // inline action 的 JSON 字符串
        bool is_inline = false;      // 是否为 inline action

        // 解析后的签名（从文件名推导）
        ActionSignature signature;

        // 是否已解析
        bool resolved = false;

        // 解析签名
        bool resolve();
    };

    // ============================================================
    // Node 签名（从文件名解析）
    // ============================================================
    struct NodeSignature {
        std::string name;                    // 自定义名称
        std::vector<DataType> params;        // 参数类型列表
        DataType return_type = DataType::VOIDDataType;
        std::string result_key;              // 返回值变量名

        std::string toString() const;
        bool validateParamCount(size_t count) const;
    };

    // ============================================================
    // L2 执行节点
    // ============================================================
    class L2Node {
    public:
        L2Node() = default;
        explicit L2Node(const nlohmann::json& json);
        explicit L2Node(const std::string& filename, const nlohmann::json& json);
        explicit L2Node(const std::string& filename, const std::string& json_str);

        // ============================================================
        // 加载和保存
        // ============================================================

        bool loadFromJson(const nlohmann::json& json);
        bool loadFromJsonString(const std::string& json_str);
        bool loadFromFile(const std::string& filepath);

        nlohmann::json toJson() const;
        std::string toJsonString(bool pretty = true) const;

        // ============================================================
        // 文件名解析（静态方法）
        // ============================================================

        static NodeSignature parseSignatureFromFilename(const std::string& filename);
        static bool isValidFilename(const std::string& filename);
        static std::string generateFilename(const std::string& name,
            const std::vector<DataType>& params,
            DataType return_type,
            const std::string& result_key = "");

        // ============================================================
        // 占位符处理
        // ============================================================

        std::vector<std::string> extractPlaceholders() const;
        std::string replacePlaceholders(const std::string& template_str,
            const std::unordered_map<std::string, std::string>& values) const;

        // ============================================================
        // 验证
        // ============================================================

        bool validate() const;
        bool validateActionRef() const;
        bool validateJudge() const;
        bool validateBranches() const;

        // ============================================================
        // Getters
        // ============================================================

        const std::string& getFilename() const { return filename_; }
        const std::string& getName() const { return name_; }
        const std::string& getDescription() const { return description_; }
        const std::vector<nlohmann::json>& getParams() const { return params_; }
        uint32_t getTimeoutMs() const { return timeout_ms_; }
        uint8_t getMaxRetries() const { return max_retries_; }
        uint16_t getRetryInterval() const { return retry_interval_; }
        const ActionRef& getAction() const { return action_; }
        const std::optional<JudgeConfig>& getJudge() const { return judge_; }
        const std::vector<nlohmann::json>& getOnSuccess() const { return on_success_; }
        const std::vector<nlohmann::json>& getOnFailure() const { return on_failure_; }
        const std::vector<nlohmann::json>& getOnTimeout() const { return on_timeout_; }
        const NodeSignature& getSignature() const { return signature_; }
        bool isInitialized() const { return initialized_; }

        // ============================================================
        // Action 类型判断（新增）
        // ============================================================

        bool isActionInline() const { return action_.is_inline; }
        const std::string& getInlineActionJson() const { return action_.inline_json; }
        bool isActionTemplate() const { return !action_.is_inline && !action_.template_path.empty(); }

        // ============================================================
        // 便捷判断
        // ============================================================

        bool hasJudge() const { return judge_.has_value() && judge_->type != JudgeType::NONE; }
        bool hasDescription() const { return !description_.empty(); }
        bool hasParams() const { return !params_.empty(); }
        bool hasReturnValue() const { return signature_.return_type != DataType::VOIDDataType; }
        bool hasResultKey() const { return !signature_.result_key.empty(); }

        // ============================================================
        // Judge 类型判断
        // ============================================================

        bool isCompareJudge() const { return hasJudge() && judge_->type == JudgeType::COMPARE; }
        bool isExpressionJudge() const { return hasJudge() && judge_->type == JudgeType::EXPRESSION; }
        bool isExistsJudge() const { return hasJudge() && judge_->type == JudgeType::EXISTS; }

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
        void parseParams(const nlohmann::json& json);
        void parseAction(const nlohmann::json& json);
        void parseJudge(const nlohmann::json& json);
        void parseBranches(const nlohmann::json& json);

        // ============================================================
        // 辅助方法
        // ============================================================

        std::vector<std::string> extractPlaceholdersFromString(const std::string& str) const;

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
        std::vector<nlohmann::json> params_;    // 参数列表（JSON格式，支持各种类型）

        // 执行控制
        uint32_t timeout_ms_ = 30000;
        uint8_t max_retries_ = 0;
        uint16_t retry_interval_ = 0;

        // Action 引用
        ActionRef action_;

        // Judge 配置（可选）
        std::optional<JudgeConfig> judge_;

        // 分支
        std::vector<nlohmann::json> on_success_;
        std::vector<nlohmann::json> on_failure_;
        std::vector<nlohmann::json> on_timeout_;

        // 签名（从文件名解析）
        NodeSignature signature_;

        // 是否已初始化
        bool initialized_ = false;
    };

    // ============================================================
    // 流输出操作符
    // ============================================================
    std::ostream& operator<<(std::ostream& os, const L2Node& node);

} // namespace industrial_config_engine