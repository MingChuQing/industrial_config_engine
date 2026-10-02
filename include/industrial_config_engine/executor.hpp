// include/industrial_config_engine/executor.hpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <optional>
#include <functional>
#include <cstdint>
#include <nlohmann/json.hpp>
#include "l1_action.hpp"
#include "l2_node.hpp"
#include "l3_group.hpp"
#include "l4_flow.hpp"
#include "action_loader.hpp"
#include "node_loader.hpp"

namespace industrial_config_engine {

    // ============================================================
    // execution status
    // ============================================================
    enum class ExecStatus {
        SUCCESS,                 // success
        FAILED,                  // structure/field validation failed
        TIMEOUT,                 // group/flow-level timeout
        STEP_BUDGET_EXCEEDED,    // step budget exceeded (suspected infinite loop)
        REFERENCE_ERROR,         // reference missing (template/node/action not found)
        CONDITION_ERROR,         // condition evaluation failed (undefined variable / invalid expression)
        INTERNAL_ERROR
    };
    const char* execStatusToString(ExecStatus s);

    // ============================================================
    // audit log (all changes and decisions recorded before execution for traceability)
    // ============================================================
    struct AuditEntry {
        uint64_t time_ms = 0;
        std::string level;      // INFO / WARN / ERROR / OPERATOR_CONFIRM / CHANGE
        std::string message;
        nlohmann::json data;
    };

    class AuditLog {
    public:
        void log(uint64_t t, const std::string& level, const std::string& msg,
            const nlohmann::json& data = nlohmann::json());
        const std::vector<AuditEntry>& entries() const { return entries_; }
        void clear() { entries_.clear(); }
        std::string toText(size_t last_n = 0) const;
    private:
        std::vector<AuditEntry> entries_;
    };

    // ============================================================
    // virtual device registry (simulation layer: register/coil read/write + behavior model)
    // ============================================================
    class DeviceRegistry {
    public:
        bool configure(const nlohmann::json& config, std::string& error);
        const nlohmann::json* connection(const std::string& device) const;
        bool hasConfiguration() const { return configured_; }
        void setOnline(const std::string& device, bool online) { online_[device] = online; }
        bool isOnline(const std::string& device) const;

        void setRegister(const std::string& device, uint16_t addr, int64_t value);
        int64_t getRegister(const std::string& device, uint16_t addr) const;
        void setCoil(const std::string& device, uint16_t addr, bool on);
        bool getCoil(const std::string& device, uint16_t addr) const;

        std::vector<std::string> devices() const;

        // device behavior model: dt_ms is the virtual time advanced this step
        using BehaviorFn = std::function<void(uint64_t dt_ms)>;
        void setBehavior(const std::string& device, BehaviorFn fn) { behaviors_[device] = std::move(fn); }
        void tick(uint64_t dt_ms);

        nlohmann::json snapshot() const;

    private:
        bool configured_ = false;
        std::map<std::string, nlohmann::json> connections_;
        std::map<std::string, std::map<uint16_t, int64_t>> registers_;
        std::map<std::string, std::map<uint16_t, bool>> coils_;
        std::map<std::string, bool> online_;
        std::map<std::string, BehaviorFn> behaviors_;
    };

    // ============================================================
    // runtime variable store (${var} resolution)
    // ============================================================
    class VariableStore {
    public:
        void set(const std::string& key, const nlohmann::json& v) { vars_[key] = v; }
        bool has(const std::string& key) const { return vars_.count(key) != 0; }
        const nlohmann::json* get(const std::string& key) const;

        // resolve ${name} placeholders; unknown names collected to missing (original placeholder kept)
        std::string resolve(const std::string& s, std::vector<std::string>* missing = nullptr) const;
        nlohmann::json resolveValue(const nlohmann::json& v,
            std::vector<std::string>* missing = nullptr) const;

        const std::unordered_map<std::string, nlohmann::json>& all() const { return vars_; }

    private:
        std::unordered_map<std::string, nlohmann::json> vars_;
    };

    // ============================================================
    // execution context
    // ============================================================
    struct ExecContext {
        DeviceRegistry devices;
        VariableStore vars;
        AuditLog audit;
        uint64_t time_ms = 0;
        size_t step_budget = 200000;   // step budget (prevent infinite loop)
        size_t steps = 0;
        bool confirm_between = false;  // operator per-item confirmation (simulated auto-confirm and audit)
        bool verbose = true;
        int indent = 0;
    };

    struct ExecResult {
        ExecStatus status = ExecStatus::SUCCESS;
        std::string message;
        uint64_t elapsed_ms = 0;    // wall-clock elapsed
        size_t steps = 0;
        uint64_t sim_time_ms = 0;   // virtual time
    };

    // ============================================================
    // Executor: load config roots, resolve templates, execute L3/L4 on virtual devices
    // parse and validate at startup, execute by data description at runtime
    // ============================================================
    class Executor {
    public:
        Executor();

        // add config root (multiple, cross-library L1/L2 reuse)
        bool addRoot(const std::string& root_dir);
        // load L1_action / L2_node under all roots
        bool loadLayers();

        // execution entry
        ExecResult runFlowFile(const std::string& flow_file, ExecContext& ctx);
        ExecResult runGroupFile(const std::string& group_file,
            const nlohmann::json& call_params, ExecContext& ctx);
        ExecResult runFlowJson(const std::string& name, const nlohmann::json& flow_json,
            ExecContext& ctx);
        ExecResult runGroupJson(const nlohmann::json& group_json,
            const nlohmann::json& call_params, ExecContext& ctx);

        // query
        const NodeLoader& nodes() const { return nodes_; }
        const ActionLoader& actions() const { return actions_; }
        const std::vector<std::string>& roots() const { return roots_; }

        // condition evaluation (static utility)
        static bool evalCondition(const nlohmann::json& cond_json, VariableStore& vars,
            std::string& err);
        static bool evalExpression(const std::string& expr, VariableStore& vars,
            std::string& err);

    private:
        // L3 group template -> file path
        bool resolveGroupPath(const std::string& templ, std::string& out_path) const;
        bool loadGroupJson(const std::string& templ, nlohmann::json& out_json);

        ExecResult execBody(const nlohmann::json& body, ExecContext& ctx, bool parallel);
        ExecResult execItem(const nlohmann::json& item, ExecContext& ctx);
        ExecResult execGroupRef(const nlohmann::json& item, ExecContext& ctx);
        ExecResult execNodeRef(const nlohmann::json& item, ExecContext& ctx);
        ExecResult execBranchItems(const nlohmann::json& branch, ExecContext& ctx);
        ExecResult execGroupMode(const nlohmann::json& g, ExecContext& ctx);
        ExecResult execAction(const L1Action& act, const nlohmann::json& args,
            ExecContext& ctx, nlohmann::json& out, bool& timed_out,
            const std::optional<std::string>& device_override);

        // utilities
        void trace(ExecContext& ctx, const std::string& msg) const;
        bool checkSteps(ExecContext& ctx, ExecResult& r) const;
        void advanceTime(ExecContext& ctx, uint64_t dt_ms);
        uint64_t timeoutValue(const nlohmann::json& v, ExecContext& ctx) const;
        std::string resolveStr(const std::string& s, ExecContext& ctx) const;
        nlohmann::json resolveValue(const nlohmann::json& v, ExecContext& ctx) const;
        static bool compareValues(const nlohmann::json& lhs, const nlohmann::json& rhs,
            const std::string& op, bool& out);
        static std::string jsonToStr(const nlohmann::json& v);
        static std::string formatArgHex(const nlohmann::json& v, int width_bytes);
        static std::string formatArg(const nlohmann::json& v, const std::string& type_name);

    private:
        std::vector<std::string> roots_;
        ActionLoader actions_;
        NodeLoader nodes_;
        std::unordered_map<std::string, nlohmann::json> group_cache_;
    };

} // namespace industrial_config_engine
