// demos/demo_simulator.cpp
// Simulator demo: three processes (T1 vacuum / T2 motor feed / T3 leak test) + fault injection
// Virtual devices + runtime variable store + virtual clock + step budget + audit log + operator per-item confirm
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

// Virtual devices and behavior model: slave 1 = vacuum gauge (example2), default = pump/valve/sensor/vacuum gauge (example1)
static void setupDevices(ExecContext& ctx) {
    ctx.devices.setOnline("default", true);
    ctx.devices.setOnline("1", true);
    ctx.devices.setRegister("default", 0x6000, 0);   // pump control register 0=stop 1=start
    ctx.devices.setRegister("default", 512, 1);      // sensor: triggered
    ctx.devices.setRegister("default", 1, 65000);    // pressure 65000 Pa (example1 vacuum register 1)
    ctx.devices.setRegister("default", 2000, 65000); // pressure 65000 Pa (no-slave read)
    ctx.devices.setRegister("1", 2000, 65000);       // pressure 65000 Pa (slave 1 read)
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
    std::cout << "[" << title << "]" << std::endl;
    std::cout << "  Status: " << execStatusToString(r.status);
    if (!r.message.empty()) std::cout << "  (" << r.message << ")";
    std::cout << std::endl;
    std::cout << "  Steps: " << r.steps << "  Sim time: " << r.sim_time_ms
        << "ms  Wall time: " << r.elapsed_ms << "ms  Audit records: " << ctx.audit.entries().size()
        << " (device writes " << countLevel(ctx, "CHANGE") << ")" << std::endl;
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
    std::cout << "Loaded L1 actions: " << ex.actions().getActionCount()
        << ", L2 nodes: " << ex.nodes().getNodeCount() << "" << std::endl;

    // ============================================================
    // T1 vacuum control process (example1, multi-mode profile)
    // ============================================================
    {
        ExecContext ctx;
        setupDevices(ctx);
        ExecResult r = ex.runFlowFile("examples/example1/L4_flow/production/vacuum_flow.json", ctx);
        std::vector<std::string> extra;
        if (ctx.vars.has("pressure")) {
            extra.push_back("Final pressure pressure = " + jsonToStrP(*ctx.vars.get("pressure")) + " Pa");
        }
        if (ctx.devices.getRegister("default", 0x6000) == 0) {
            extra.push_back("Pump register 0x6000 = 0 (stopped)");
        }
        printResult("T1 vacuum control process (example1)", r, ctx, extra);
    }

    // ============================================================
    // T2 motor feeding process (example2 complete feeding group)
    // ============================================================
    {
        ExecContext ctx;
        setupDevices(ctx);
        ExecResult r = ex.runGroupFile("examples/example2/L3_group/complete_feeding.group.json",
            nlohmann::json::array(), ctx);
        std::vector<std::string> extra;
        if (ctx.vars.has("position_1")) extra.push_back("position_1 = " + jsonToStrP(*ctx.vars.get("position_1")));
        if (ctx.vars.has("position_2")) extra.push_back("position_2 = " + jsonToStrP(*ctx.vars.get("position_2")));
        printResult("T2 motor feeding process (example2)", r, ctx, extra);
    }

    // ============================================================
    // T3 leak test process (example3, per-item confirm + audit)
    // ============================================================
    {
        ExecContext ctx;
        setupDevices(ctx);
        ctx.confirm_between = true;
        ExecResult r = ex.runFlowFile("examples/example3/L4_flow/leak_test/production_flow.json", ctx);
        std::vector<std::string> extra;
        if (ctx.vars.has("test_result")) {
            extra.push_back("Test result test_result = " + jsonToStrP(*ctx.vars.get("test_result")));
        }
        extra.push_back("Operator confirm records: " + std::to_string(countLevel(ctx, "OPERATOR_CONFIRM")) + "");
        printResult("T3 leak test process (example3, per-item confirm)", r, ctx, extra);
    }

    // ============================================================
    // Fault injection: verify interception of the three gates
    // ============================================================
    std::cout << "\n========================================" << std::endl;
    std::cout << "[Fault injection: three-gate interception demo]" << std::endl;

    // (a) loop condition references undefined variable (typo) -> CONDITION_ERROR
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
        printResult("Fault 1: condition references undefined variable (typo)", r, ctx);
    }

    // (b) always-true loop condition -> step budget exceeded (suspected infinite loop)
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
        printResult("Fault 2: always-true loop (suspected infinite loop)", r, ctx);
    }

    // (c) reference to nonexistent node -> REFERENCE_ERROR
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
        printResult("Fault 3: referenced node definition missing", r, ctx);
    }

    std::cout << "\nAll scenarios finished. Reproduce with: build\\simcheck\\bin\\demo_simulator.exe [root...]" << std::endl;
    return 0;
}
