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
    // 执行状态
    // ============================================================
    enum class ExecStatus {
        SUCCESS,                 // 成功
        FAILED,                  // 结构/字段校验失败
        TIMEOUT,                 // 组/流程级超时
        STEP_BUDGET_EXCEEDED,    // 步数预算超限（疑似死循环）
        REFERENCE_ERROR,         // 引用缺失（模板/节点/动作未找到）
        CONDITION_ERROR,         // 条件求值失败（未定义变量/非法表达式）
        INTERNAL_ERROR
    };
    const char* execStatusToString(ExecStatus s);

    // ============================================================
    // 审计日志（所有更改与决策在执行前记录，供追溯）
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
    // 虚拟设备注册表（仿真层：寄存器/线圈读写 + 行为模型）
    // ============================================================
    class DeviceRegistry {
    public:
        void setOnline(const std::string& device, bool online) { online_[device] = online; }
        bool isOnline(const std::string& device) const;

        void setRegister(const std::string& device, uint16_t addr, int64_t value);
        int64_t getRegister(const std::string& device, uint16_t addr) const;
        void setCoil(const std::string& device, uint16_t addr, bool on);
        bool getCoil(const std::string& device, uint16_t addr) const;

        std::vector<std::string> devices() const;

        // 设备行为模型：dt_ms 为本次推进的虚拟时间
        using BehaviorFn = std::function<void(uint64_t dt_ms)>;
        void setBehavior(const std::string& device, BehaviorFn fn) { behaviors_[device] = std::move(fn); }
        void tick(uint64_t dt_ms);

        nlohmann::json snapshot() const;

    private:
        std::map<std::string, std::map<uint16_t, int64_t>> registers_;
        std::map<std::string, std::map<uint16_t, bool>> coils_;
        std::map<std::string, bool> online_;
        std::map<std::string, BehaviorFn> behaviors_;
    };

    // ============================================================
    // 运行时变量存储（${变量} 解析）
    // ============================================================
    class VariableStore {
    public:
        void set(const std::string& key, const nlohmann::json& v) { vars_[key] = v; }
        bool has(const std::string& key) const { return vars_.count(key) != 0; }
        const nlohmann::json* get(const std::string& key) const;

        // 解析 ${name} 占位符；未知变量名收集到 missing（原占位符保留）
        std::string resolve(const std::string& s, std::vector<std::string>* missing = nullptr) const;
        nlohmann::json resolveValue(const nlohmann::json& v,
            std::vector<std::string>* missing = nullptr) const;

        const std::unordered_map<std::string, nlohmann::json>& all() const { return vars_; }

    private:
        std::unordered_map<std::string, nlohmann::json> vars_;
    };

    // ============================================================
    // 执行上下文
    // ============================================================
    struct ExecContext {
        DeviceRegistry devices;
        VariableStore vars;
        AuditLog audit;
        uint64_t time_ms = 0;
        size_t step_budget = 200000;   // 步数预算（防止死循环）
        size_t steps = 0;
        bool confirm_between = false;  // 操作员逐项确认（仿真自动确认并写审计）
        bool verbose = true;
        int indent = 0;
    };

    struct ExecResult {
        ExecStatus status = ExecStatus::SUCCESS;
        std::string message;
        uint64_t elapsed_ms = 0;    // 墙钟耗时
        size_t steps = 0;
        uint64_t sim_time_ms = 0;   // 虚拟时间
    };

    // ============================================================
    // 执行器：加载配置根目录、解析模板、在虚拟设备上执行 L3/L4
    // 启动时解析与校验，运行时按数据描述执行
    // ============================================================
    class Executor {
    public:
        Executor();

        // 添加配置根目录（可多个，支持跨库复用 L1/L2）
        bool addRoot(const std::string& root_dir);
        // 加载所有根目录下的 L1_action / L2_node
        bool loadLayers();

        // 执行入口
        ExecResult runFlowFile(const std::string& flow_file, ExecContext& ctx);
        ExecResult runGroupFile(const std::string& group_file,
            const nlohmann::json& call_params, ExecContext& ctx);
        ExecResult runFlowJson(const std::string& name, const nlohmann::json& flow_json,
            ExecContext& ctx);
        ExecResult runGroupJson(const nlohmann::json& group_json,
            const nlohmann::json& call_params, ExecContext& ctx);

        // 查询
        const NodeLoader& nodes() const { return nodes_; }
        const ActionLoader& actions() const { return actions_; }
        const std::vector<std::string>& roots() const { return roots_; }

        // 条件求值（静态工具）
        static bool evalCondition(const nlohmann::json& cond_json, VariableStore& vars,
            std::string& err);
        static bool evalExpression(const std::string& expr, VariableStore& vars,
            std::string& err);

    private:
        // L3 组模板 → 文件路径
        bool resolveGroupPath(const std::string& templ, std::string& out_path) const;
        bool loadGroupJson(const std::string& templ, nlohmann::json& out_json);

        ExecResult execBody(const nlohmann::json& body, ExecContext& ctx, bool parallel);
        ExecResult execItem(const nlohmann::json& item, ExecContext& ctx);
        ExecResult execGroupRef(const nlohmann::json& item, ExecContext& ctx);
        ExecResult execNodeRef(const nlohmann::json& item, ExecContext& ctx);
        ExecResult execBranchItems(const nlohmann::json& branch, ExecContext& ctx);
        ExecResult execGroupMode(const nlohmann::json& g, ExecContext& ctx);
        ExecResult execAction(const L1Action& act, const nlohmann::json& args,
            ExecContext& ctx, nlohmann::json& out, bool& timed_out);

        // 工具
        void trace(ExecContext& ctx, const std::string& msg) const;
        bool checkSteps(ExecContext& ctx, ExecResult& r) const;
        void advanceTime(ExecContext& ctx, uint64_t dt_ms);
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
