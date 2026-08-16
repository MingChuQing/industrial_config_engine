// demos/demo_simulator.cpp
// 仿真运行器演示：三条工艺（T1真空控制 / T2电机上料 / T3泄漏测试）+ 故障注入
// 虚拟设备 + 运行时变量存储 + 虚拟时钟 + 步数预算 + 审计日志 + 操作员逐项确认
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

// 虚拟设备与行为模型：slave 1 = 真空计（example2），default = 泵/阀/光电/真空计（example1）
static void setupDevices(ExecContext& ctx) {
    ctx.devices.setOnline("default", true);
    ctx.devices.setOnline("1", true);
    ctx.devices.setRegister("default", 0x6000, 0);   // 泵控制寄存器 0=停 1=启
    ctx.devices.setRegister("default", 512, 1);      // 光电开关：已触发
    ctx.devices.setRegister("default", 1, 65000);    // 压力 65000 Pa（example1 真空计寄存器 1）
    ctx.devices.setRegister("default", 2000, 65000); // 压力 65000 Pa（无从站读法）
    ctx.devices.setRegister("1", 2000, 65000);       // 压力 65000 Pa（slave 1 读法）
    ctx.devices.setBehavior("default", [&ctx](uint64_t) {
        if (ctx.devices.getRegister("default", 0x6000) == 1) {
            for (uint16_t addr : { uint16_t(1), uint16_t(2000) }) {
                int64_t p = ctx.devices.getRegister("default", addr);
                int64_t decay = std::max<int64_t>(1, p / 40);
                ctx.devices.setRegister("default", addr, std::max<int64_t>(0, p - decay));
            }
        }
    });
    ctx.devices.setBehavior("1", [&ctx](uint64_t) {
        if (ctx.devices.getRegister("default", 0x6000) == 1) {
            int64_t p = ctx.devices.getRegister("1", 2000);
            int64_t decay = std::max<int64_t>(1, p / 40);
            ctx.devices.setRegister("1", 2000, std::max<int64_t>(0, p - decay));
        }
    });
}

static size_t countLevel(ExecContext& ctx, const std::string& level) {
    size_t n = 0;
    for (const auto& e : ctx.audit.entries()) {
        if (e.level == level) ++n;
    }
    return n;
}

static void printResult(const std::string& title, const ExecResult& r, ExecContext& ctx,
    const std::vector<std::string>& extra = {}) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "【" << title << "】" << std::endl;
    std::cout << "  状态: " << execStatusToString(r.status);
    if (!r.message.empty()) std::cout << "  (" << r.message << ")";
    std::cout << std::endl;
    std::cout << "  仿真步数: " << r.steps << "  虚拟时间: " << r.sim_time_ms
        << "ms  墙钟耗时: " << r.elapsed_ms << "ms  审计记录: " << ctx.audit.entries().size()
        << " 条（设备写入 " << countLevel(ctx, "CHANGE") << " 条）" << std::endl;
    for (const auto& s : extra) std::cout << "  " << s << std::endl;
}

int main(int argc, char** argv) {
    std::vector<std::string> roots;
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) roots.push_back(argv[i]);
    }
    else {
        roots.push_back("examples/example1");
        roots.push_back("examples/example2");
        roots.push_back("examples/example3");
    }

    Executor ex;
    for (const auto& r : roots) ex.addRoot(r);
    ex.loadLayers();
    std::cout << "已加载 L1 动作: " << ex.actions().getActionCount()
        << " 个, L2 节点: " << ex.nodes().getNodeCount() << " 个" << std::endl;

    // ============================================================
    // T1 真空控制工艺（example1，多模式 profile）
    // ============================================================
    {
        ExecContext ctx;
        setupDevices(ctx);
        ExecResult r = ex.runFlowFile("examples/example1/L4_flow/production/vacuum_flow.json", ctx);
        std::vector<std::string> extra;
        if (ctx.vars.has("pressure")) {
            extra.push_back("最终压力 pressure = " + jsonToStrP(*ctx.vars.get("pressure")) + " Pa");
        }
        if (ctx.devices.getRegister("default", 0x6000) == 0) {
            extra.push_back("泵控制寄存器 0x6000 = 0（已停止）");
        }
        printResult("T1 真空控制工艺（example1）", r, ctx, extra);
    }

    // ============================================================
    // T2 电机上料工艺（example2 完整上料主组）
    // ============================================================
    {
        ExecContext ctx;
        setupDevices(ctx);
        ExecResult r = ex.runGroupFile("examples/example2/L3_group/complete_feeding.group.json",
            nlohmann::json::array(), ctx);
        std::vector<std::string> extra;
        if (ctx.vars.has("position_1")) extra.push_back("position_1 = " + jsonToStrP(*ctx.vars.get("position_1")));
        if (ctx.vars.has("position_2")) extra.push_back("position_2 = " + jsonToStrP(*ctx.vars.get("position_2")));
        printResult("T2 电机上料工艺（example2）", r, ctx, extra);
    }

    // ============================================================
    // T3 泄漏测试工艺（example3，操作员逐项确认 + 审计）
    // ============================================================
    {
        ExecContext ctx;
        setupDevices(ctx);
        ctx.confirm_between = true;
        ExecResult r = ex.runFlowFile("examples/example3/L4_flow/leak_test/production_flow.json", ctx);
        std::vector<std::string> extra;
        if (ctx.vars.has("test_result")) {
            extra.push_back("测试结果 test_result = " + jsonToStrP(*ctx.vars.get("test_result")));
        }
        extra.push_back("操作员确认记录: " + std::to_string(countLevel(ctx, "OPERATOR_CONFIRM")) + " 条");
        printResult("T3 泄漏测试工艺（example3，逐项确认）", r, ctx, extra);
    }

    // ============================================================
    // 故障注入：验证三个验证关卡的拦截能力
    // ============================================================
    std::cout << "\n========================================" << std::endl;
    std::cout << "【故障注入：三维验证拦截演示】" << std::endl;

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
        printResult("注入1: 条件引用未定义变量（拼写错误）", r, ctx);
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
        printResult("注入2: 恒真循环（疑似死循环）", r, ctx);
    }

    // (c) 引用不存在的节点 → REFERENCE_ERROR
    {
        ExecContext ctx;
        setupDevices(ctx);
        nlohmann::json bug = {
            {"type", "group"}, {"name", "bug_missing_node"}, {"mode", "sequence"},
            {"body", nlohmann::json::array({
                {{"type", "node"}, {"template", "L2_node/vacuum_nodes.json/read_pressureX.r_u16_pressure"},
                 {"params", nlohmann::json::array()}}
            })}
        };
        ExecResult r = ex.runGroupJson(bug, nlohmann::json::array(), ctx);
        printResult("注入3: 引用的节点定义缺失", r, ctx);
    }

    std::cout << "\n全部场景执行完毕。可运行 build\\simcheck\\bin\\demo_simulator.exe [根目录...] 复现。" << std::endl;
    return 0;
}
