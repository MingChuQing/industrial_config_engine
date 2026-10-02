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
    // Judge type enumeration
    // ============================================================
    enum class JudgeType {
        NONE,       // No judgment, defaults to success
        COMPARE,    // Value comparison
        EXPRESSION, // Expression evaluation
        EXISTS      // Existence check
    };

    // ============================================================
    // Judge configuration
    // ============================================================
    struct JudgeConfig {
        JudgeType type = JudgeType::NONE;
        std::string condition;      // compare: eq, ne, lt, le, gt, ge, abs_ge, abs_gt, abs_le, abs_lt
        std::string value;          // compare: comparison value; expression: expression
        std::string expression;     // expression: expression string

        // Determine whether this is a valid configuration
        bool isValid() const;

        // Get placeholders
        std::vector<std::string> getPlaceholders() const;
    };

    // ============================================================
    // Action reference (L1 atomic action reference)
    // ============================================================
    struct ActionRef {
        std::string template_path;   // L1 Action file path (template reference)
        std::string inline_json;     // JSON string of the inline action
        bool is_inline = false;      // Whether this is an inline action

        // Resolved signature (derived from the filename)
        ActionSignature signature;

        // Whether it has been resolved
        bool resolved = false;

        // Resolve the signature
        bool resolve();
    };

    // ============================================================
    // Node signature (parsed from the filename)
    // ============================================================
    struct NodeSignature {
        std::string name;                    // Custom name
        std::vector<DataType> params;        // Parameter type list
        DataType return_type = DataType::VOIDDataType;
        std::string result_key;              // Return value variable name

        std::string toString() const;
        bool validateParamCount(size_t count) const;
    };

    // ============================================================
    // L2 execution node
    // ============================================================
    class L2Node {
    public:
        L2Node() = default;
        explicit L2Node(const nlohmann::json& json);
        explicit L2Node(const std::string& filename, const nlohmann::json& json);
        explicit L2Node(const std::string& filename, const std::string& json_str);

        // ============================================================
        // Loading and saving
        // ============================================================

        bool loadFromJson(const nlohmann::json& json);
        bool loadFromJsonString(const std::string& json_str);
        bool loadFromFile(const std::string& filepath);

        nlohmann::json toJson() const;
        std::string toJsonString(bool pretty = true) const;

        // ============================================================
        // Filename parsing (static methods)
        // ============================================================

        static NodeSignature parseSignatureFromFilename(const std::string& filename);
        static bool isValidFilename(const std::string& filename);
        static std::string generateFilename(const std::string& name,
            const std::vector<DataType>& params,
            DataType return_type,
            const std::string& result_key = "");

        // ============================================================
        // Placeholder handling
        // ============================================================

        std::vector<std::string> extractPlaceholders() const;
        std::string replacePlaceholders(const std::string& template_str,
            const std::unordered_map<std::string, std::string>& values) const;

        // ============================================================
        // Validation
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
        const std::optional<std::string>& getDevice() const { return device_; }
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
        // Action type predicates (new)
        // ============================================================

        bool isActionInline() const { return action_.is_inline; }
        const std::string& getInlineActionJson() const { return action_.inline_json; }
        bool isActionTemplate() const { return !action_.is_inline && !action_.template_path.empty(); }

        // ============================================================
        // Convenience predicates
        // ============================================================

        bool hasJudge() const { return judge_.has_value() && judge_->type != JudgeType::NONE; }
        bool hasDescription() const { return !description_.empty(); }
        bool hasParams() const { return !params_.empty(); }
        bool hasReturnValue() const { return signature_.return_type != DataType::VOIDDataType; }
        bool hasResultKey() const { return !signature_.result_key.empty(); }

        // ============================================================
        // Judge type predicates
        // ============================================================

        bool isCompareJudge() const { return hasJudge() && judge_->type == JudgeType::COMPARE; }
        bool isExpressionJudge() const { return hasJudge() && judge_->type == JudgeType::EXPRESSION; }
        bool isExistsJudge() const { return hasJudge() && judge_->type == JudgeType::EXISTS; }

        // ============================================================
        // Debugging and printing
        // ============================================================

        std::string toString() const;
        void print(std::ostream& os = std::cout) const;

    private:
        // ============================================================
        // Private parsing methods
        // ============================================================

        void parseCommonFields(const nlohmann::json& json);
        void parseParams(const nlohmann::json& json);
        void parseAction(const nlohmann::json& json);
        void parseJudge(const nlohmann::json& json);
        void parseBranches(const nlohmann::json& json);

        // ============================================================
        // Helper methods
        // ============================================================

        std::vector<std::string> extractPlaceholdersFromString(const std::string& str) const;

    private:
        // ============================================================
        // Member variables
        // ============================================================

        // File information
        std::string filename_;
        std::string filepath_;

        // Core fields
        std::string name_;
        std::string description_;
        std::optional<std::string> device_;
        std::vector<nlohmann::json> params_;    // Parameter list (JSON format, supports various types)

        // Execution control
        uint32_t timeout_ms_ = 30000;
        uint8_t max_retries_ = 0;
        uint16_t retry_interval_ = 0;

        // Action reference
        ActionRef action_;

        // Judge configuration (optional)
        std::optional<JudgeConfig> judge_;

        // Branches
        std::vector<nlohmann::json> on_success_;
        std::vector<nlohmann::json> on_failure_;
        std::vector<nlohmann::json> on_timeout_;

        // Signature (parsed from the filename)
        NodeSignature signature_;

        // Whether it has been initialized
        bool initialized_ = false;
    };

    // ============================================================
    // Stream output operator
    // ============================================================
    std::ostream& operator<<(std::ostream& os, const L2Node& node);

} // namespace industrial_config_engine
