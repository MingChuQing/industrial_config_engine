// demos/demo_simulator.cpp
// 仿真运行器演示：虚拟设备 + 运行时变量存储 + 虚拟时钟 + 步数预算 + 审计日志
#include "industrial_config_engine/executor.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

using namespace industrial_config_engine;

static std::string jsonToStrP(const nlohmann::json& v) {
    if (v.is_null()) return "null";
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    return v.dump();
}

// 虚拟设备与行为模型：slave 1 = 真空计（泵运行时压力指数衰减），default = 泵/阀
static void setupDevices(ExecContext& ctx) {
    ctx.devices.setOnline("default", true);
    ctx.devices.setOnline("1", true);
    ctx.devices.setRegister("default", 0x6000, 0);   // 泵控制寄存器 0=停 1=启
    ctx.devices.setRegister("1", 2000, 65000);       // 压力 65000 Pa
    ctx.devices.setBehavior("1", [&ctx](uint64_t) {
        if (ctx.devices.getRegister("default", 0x6000) == 1) {
            int64_t p = ctx.devices.getRegister("1", 2000);
            int64_t decay = std::max<int64_t>(1, p / 40);
            ctx.devices.setRegister("1", 2000, std::max<int64_t>(0, p - decay));
        }
    });
}

static void printResult(const std::string& title, const ExecResult& r) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "【" << title << "】" << std::endl;
    std::cout << "  状态: " << execStatusToString(r.status);
    if (!r.message.empty()) std::cout << "  (" << r.message << ")";
    std::cout << std::endl;
    std::cout << "  仿真步数: " << r.steps << "  虚拟时间: " << r.sim_time_ms
        << "ms  墙钟耗时: " << r.elapsed_ms << "ms" << std::endl;
}

static void printAuditTail(ExecContext& ctx, size_t n = 8) {
    std::cout << "  --- 审计日志（最近 " << n << " 条）---" << std::endl;
    const auto& entries = ctx.audit.entries();
    size_t start = entries.size() > n ? entries.size() - n : 0;
    for (size_t i = start; i < entries.size(); ++i) {
        std::cout << "  [t=" << entries[i].time_ms << "ms] [" << entries[i].level << "] "
            << entries[i].message << std::endl;
    }
}

int main(int argc, char** argv) {
    std::vector<std::string> roots;
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) roots.push_back(argv[i]);
    }
    else {
        roots.push_back("examples/example2");
        roots.push_back("examples/example3");
    }

    Executor ex;
    for (const auto& r : roots) ex.addRoot(r);
    ex.loadLayers();
    std::cout << "已加载 L1 动作: " << ex.actions().getActionCount()
        << " 个, L2 节点: " << ex.nodes().getNodeCount() << " 个" << std::endl;

    // ============================================================
    // 场景 1：真空工艺主流程（example2）——正常收敛
    // ============================================================
    {
        ExecContext ctx;
        setupDevices(ctx);
        ExecResult r = ex.runFlowFile("examples/example2/L4_flow/main_flow.json", ctx);
        printResult("场景1: 真空工艺主流程（正常收敛）", r);
        if (ctx.vars.has("pressure")) {
            std::cout << "  最终压力变量 pressure = "
                << jsonToStrP(*ctx.vars.get("pressure")) << " Pa (≤5 循环正常退出)" << std::endl;
        }
        if (ctx.devices.getRegister("default", 0x6000) == 0) {
            std::cout << "  泵控制寄存器 0x6000 = 0（已停止）" << std::endl;
        }
        printAuditTail(ctx, 6);
    }

    // ============================================================
    // 场景 2：气密性检测生产流程（example3）——操作员逐项确认 + 审计
    // ============================================================
    {
        ExecContext ctx;
        setupDevices(ctx);
        ctx.confirm_between = true;
        ExecResult r = ex.runFlowFile("examples/example3/L4_flow/leak_test/production_flow.json", ctx);
        printResult("场景2: 气密性检测生产流程（逐项确认）", r);
        if (ctx.vars.has("test_result")) {
            std::cout << "  测试结果 test_result = "
                << jsonToStrP(*ctx.vars.get("test_result")) << std::endl;
        }
        size_t confirms = 0;
        for (const auto& e : ctx.audit.entries()) {
            if (e.level == "OPERATOR_CONFIRM") ++confirms;
        }
        std::cout << "  操作员确认记录: " << confirms << " 条" << std::endl;
        printAuditTail(ctx, 5);
    }

    // ============================================================
    // 场景 3：错误注入——复现论文表四的三类拦截
    // ============================================================
    std::cout << "\n========================================" << std::endl;
    std::cout << "【场景3: 错误注入演示】" << std::endl;

    // (a) 循环条件引用未定义变量（拼写错误）→ CONDITION_ERROR
    {
        ExecContext ctx;
        setupDevices(ctx);
        nlohmann::json bug = {
            {"type", "group"}, {"name", "bug_typo"}, {"mode", "loop"},
            {"loop", {{"type", "do-while"}, {"condition", "${pressue} > 5"}, {"max_iterations", 100}}},
            {"body", nlohmann::json::array({
                {{"type", "node"}, {"template", "L2_node/wait_nodes.json/wait_ms.u16.r_b_wait_done"}, {"params", {50}}}
            })}
        };
        ExecResult r = ex.runGroupJson(bug, nlohmann::json::array(), ctx);
        printResult("3a: 循环条件引用未定义变量（拼写错误）", r);
    }

    // (b) 恒真循环条件 → 步数预算超限（疑似死循环）
    {
        ExecContext ctx;
        setupDevices(ctx);
        nlohmann::json bug = {
            {"type", "group"}, {"name", "bug_infinite"}, {"mode", "loop"},
            {"loop", {{"type", "while"}, {"condition", "true"}, {"max_iterations", 100}}},
            {"body", nlohmann::json::array({
                {{"type", "node"}, {"template", "L2_node/wait_nodes.json/wait_ms.u16.r_b_wait_done"}, {"params", {10}}}
            })}
        };
        ExecResult r = ex.runGroupJson(bug, nlohmann::json::array(), ctx);
        printResult("3b: 恒真循环（疑似死循环）", r);
    }

    // (c) 引用不存在的节点 → REFERENCE_ERROR
    {
        ExecContext ctx;
        setupDevices(ctx);
        nlohmann::json bug = {
            {"type", "group"}, {"name", "bug_missing_node"}, {"mode", "sequence"},
            {"body", nlohmann::json::array({
                {{"type", "node"}, {"template", "L2_node/vacuum_nodes.json/read_pressureX.r_u16_pressure"}, {"params", {} }}
            })}
        };
        ExecResult r = ex.runGroupJson(bug, nlohmann::json::array(), ctx);
        printResult("3c: 引用的节点定义缺失", r);
    }

    std::cout << "\n全部场景执行完毕。" << std::endl;
    return 0;
}
