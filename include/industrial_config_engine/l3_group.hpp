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
    // Control flow mode enumeration
    // ============================================================
    enum class GroupMode {
        SEQUENCE,   // Sequential execution
        PARALLEL,   // Parallel execution
        LOOP,       // Loop execution
        IF,         // Conditional branch
        SWITCH      // Multi-condition branch
    };

    // ============================================================
    // Loop type enumeration
    // ============================================================
    enum class LoopType {
        COUNT,      // Fixed number of iterations
        WHILE,      // Loop while the condition is true
        UNTIL,      // Stop when the condition becomes true
        FOREACH,    // Iterate over a list
        DOWHILE     // Execute first, then check
    };

    // ============================================================
    // Loop configuration
    // ============================================================
    struct LoopConfig {
        LoopType type = LoopType::COUNT;
        int count = 0;                  // COUNT: number of iterations
        std::string condition;          // WHILE/UNTIL: condition expression
        std::string items;              // FOREACH: list variable
        std::string item_name;          // FOREACH: current item variable name
        int max_iterations = 10000;     // Maximum iterations (prevents infinite loop)
    };

    // ============================================================
    // Condition configuration
    // ============================================================
    struct ConditionConfig {
        std::string type;               // compare, expression, exists, choice, value
        std::string condition;          // compare condition: eq, ne, lt, le, gt, ge
        std::string value;              // compare: comparison value; choice: matching value
        std::string source;             // value: variable source
        std::string expression;         // expression: expression
        std::string variable;           // exists: variable name
    };

    // ============================================================
    // Case branch (used for switch)
    // ============================================================
    struct SwitchCase {
        std::string case_value;         // Matching value (string form)
        nlohmann::json body;            // Branch body
    };

    // ============================================================
    // Argument definition
    // ============================================================
    struct GroupArgDef {
        int index = 0;
        std::string name;
        DataType type = DataType::VOIDDataType;
        std::optional<std::string> default_value;
        std::string desc;
    };

    // ============================================================
    // L3 execution group
    // ============================================================
    class L3Group {
    public:
        L3Group() = default;
        explicit L3Group(const nlohmann::json& json);
        explicit L3Group(const std::string& filename, const nlohmann::json& json);
        explicit L3Group(const std::string& filename, const std::string& json_str);

        // ============================================================
        // Loading and saving
        // ============================================================

        bool loadFromJson(const nlohmann::json& json);
        bool loadFromJsonString(const std::string& json_str);
        bool loadFromFile(const std::string& filepath);

        nlohmann::json toJson() const;
        std::string toJsonString(bool pretty = true) const;

        // ============================================================
        // Validation
        // ============================================================

        bool validate() const;
        bool validateBody() const;
        bool validateLoop() const;
        bool validateCondition() const;
        bool validateCases() const;
        bool validateMaxNodes() const;
        bool validateCircularReference(const std::vector<std::string>& ancestors = {}) const;

        // ============================================================
        // Node counting
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

        // Loop related
        bool hasLoop() const { return loop_.has_value(); }
        const std::optional<LoopConfig>& getLoop() const { return loop_; }

        // Condition related
        bool hasCondition() const { return condition_.has_value(); }
        const std::optional<ConditionConfig>& getCondition() const { return condition_; }

        // Branch related
        bool hasThen() const { return !then_body_.is_null(); }
        bool hasElse() const { return !else_body_.is_null(); }
        const nlohmann::json& getThen() const { return then_body_; }
        const nlohmann::json& getElse() const { return else_body_; }

        // Switch related
        bool hasCases() const { return !cases_.empty(); }
        bool hasDefault() const { return !default_body_.is_null(); }
        const std::vector<SwitchCase>& getCases() const { return cases_; }
        const nlohmann::json& getDefault() const { return default_body_; }

        // ============================================================
        // Parameter parsing
        // ============================================================

        std::vector<std::string> extractPlaceholders() const;
        std::string replacePlaceholders(const std::string& template_str,
            const std::unordered_map<std::string, std::string>& values) const;

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
        void parseArgs(const nlohmann::json& json);
        void parseBody(const nlohmann::json& json);
        void parseLoop(const nlohmann::json& json);
        void parseCondition(const nlohmann::json& json);
        void parseThenElse(const nlohmann::json& json);
        void parseSwitch(const nlohmann::json& json);

        // ============================================================
        // Helper methods
        // ============================================================

        std::vector<std::string> extractPlaceholdersFromString(const std::string& str) const;
        std::string modeToString(GroupMode mode) const;
        GroupMode stringToMode(const std::string& mode_str) const;
        std::string loopTypeToString(LoopType type) const;
        LoopType stringToLoopType(const std::string& type_str) const;
        int countNodesInJson(const nlohmann::json& body) const;

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
        GroupMode mode_ = GroupMode::SEQUENCE;
        int max_nodes_ = 100;
        int delay_between_ = 0;
        bool confirm_between_ = false;

        // Body (contains L2 Node or L3 Group references)
        nlohmann::json body_;

        // Argument definitions
        std::vector<GroupArgDef> args_;

        // Loop configuration (used when mode = LOOP)
        std::optional<LoopConfig> loop_;

        // Condition configuration (used when mode = IF)
        std::optional<ConditionConfig> condition_;

        // Branch body (used when mode = IF)
        nlohmann::json then_body_;
        nlohmann::json else_body_;

        // Switch branch (used when mode = SWITCH)
        std::vector<SwitchCase> cases_;
        nlohmann::json default_body_;

        // Whether it has been initialized
        bool initialized_ = false;
    };

    // ============================================================
    // L3 Group loader
    // ============================================================
    class L3GroupLoader {
    public:
        L3GroupLoader() = default;

        // Load a single Group file
        L3Group loadFromFile(const std::string& filepath);

        // Load all Group files in a directory
        std::vector<L3Group> loadFromDirectory(const std::string& dir_path);

        // Load a Group Bundle (aggregate file)
        std::vector<L3Group> loadBundle(const std::string& bundle_path);

        // Resolve the reference path
        L3Group resolveReference(const std::string& template_path,
            const std::vector<nlohmann::json>& params = {});

        // Set the L2 Node directory (used to resolve Node references)
        void setL2NodeDir(const std::string& dir) { l2_node_dir_ = dir; }

        // Set the L3 Group directory (used to resolve Group references)
        void setL3GroupDir(const std::string& dir) { l3_group_dir_ = dir; }

        // Set the cache
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
    // Stream output operator
    // ============================================================
    std::ostream& operator<<(std::ostream& os, const L3Group& group);

} // namespace industrial_config_engine