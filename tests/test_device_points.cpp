#include "industrial_config_engine/executor.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>

using namespace industrial_config_engine;
using json = nlohmann::json;
static void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
static json read(const std::string& path) {
    std::ifstream in(path); json value; in >> value; return value;
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Pass the example4 directory");
        const std::string root = argv[1];
        const auto registry = read(root + "/device_registry.json");
        const auto variables = read(root + "/system_variables.json");
        const auto trigger = read(root + "/L3_group/trigger_motor_pr.group.json");
        VariableStore vars;
        vars.set("rows", json::array({{{"motors", json::array({{{"device", "X"}, {"pr", 15}}})}}}));
        vars.set("index", 0);
        check(vars.resolveValue("${rows[index].motors}").is_array(), "Native array lookup");
        check(vars.resolveValue("${rows[0].motors[0].device}") == "X", "Nested member lookup");
        check(vars.get("rows[1]") == nullptr && vars.get("rows[-1]") == nullptr,
              "Bounds check");
        vars.set("index", -1);
        check(vars.get("rows[index]") == nullptr && vars.get("rows[0].") == nullptr,
              "Malformed and negative paths");
        vars.set("profile.x", 42);
        check(vars.resolveValue("${profile.x}") == 42, "Literal key compatibility");
        DeviceRegistry devices;
        std::string error;
        check(devices.configure(registry, error), error);
        auto duplicate = registry; duplicate["devices"]["Y1"] = duplicate["devices"]["X"];
        check(!devices.configure(duplicate, error), "Duplicate endpoint accepted");
        check(*devices.connection("Y1") == registry["devices"]["Y1"], "Failed configure must be atomic");
        for (int invalid : {0, 248}) {
            auto bad = registry; bad["devices"]["X"]["address"] = invalid;
            check(!devices.configure(bad, error), "Invalid station accepted");
        }
        auto node_json = read(root + "/L2_node/all_nodes.json")["nodes"][1];
        node_json["device"] = "${motor_device}";
        L2Node node(node_json);
        check(node.getDevice() == "${motor_device}" && node.toJson()["device"] == "${motor_device}",
              "L2 device round trip");
        for (const auto& invalid : json::array({42, "", "   "})) {
            auto bad = node_json; bad["device"] = invalid;
            L2Node rejected;
            check(!rejected.loadFromJson(bad), "Invalid L2 device accepted");
        }
        Executor executor;
        check(executor.addRoot(root) && executor.loadLayers(), "Load configuration");
        auto context = [&]() {
            ExecContext ctx; ctx.verbose = false;
            check(ctx.devices.configure(registry, error), error);
            for (auto it = variables.begin(); it != variables.end(); ++it) ctx.vars.set(it.key(), it.value());
            return ctx;
        };
        // Routing is independent of device kind. Reuse one read PDU as a
        // simulation-only routing probe for every registered alias.
        const auto node_definitions = read(root + "/L2_node/all_nodes.json")["nodes"];
        std::string read_template;
        for (const auto& definition : node_definitions) {
            const auto action = definition["action"]["template"].get<std::string>();
            if (action.find("/read_fc03_reg602c.r_i32_value") != std::string::npos)
                read_template = "L2_node/all_nodes.json/" + definition["filename"].get<std::string>();
        }
        check(!read_template.empty(), "Read routing probe missing");
        for (auto device = registry["devices"].begin(); device != registry["devices"].end(); ++device) {
            auto ctx = context();
            json call = {{"type", "node"}, {"template", read_template}, {"device", device.key()}, {"params", json::array()}};
            json group = {{"type", "group"}, {"name", "route probe"}, {"mode", "sequence"}, {"body", json::array({call})}};
            auto result = executor.runGroupJson(group, json::array(), ctx);
            check(result.status == ExecStatus::SUCCESS, result.message);
            int routes = 0;
            for (const auto& entry : ctx.audit.entries()) if (entry.level == "ROUTE") {
                ++routes;
                check(entry.data["device"] == device.key() && entry.data["port"] == device.value()["port"] &&
                      entry.data["address"] == device.value()["address"] && entry.data["request"] == "03 60 2C 00 02",
                      "Device binding modified PDU or resolved wrong endpoint");
            }
            check(routes == 1, "Missing device route");
        }
        for (const auto* motor : {"X", "Y1", "Y2", "Z"}) {
            for (int point : {0, 15}) {
                auto ctx = context();
                auto result = executor.runGroupJson(trigger, json::array({motor, point}), ctx);
                check(result.status == ExecStatus::SUCCESS, result.message);
                check(ctx.devices.getRegister(motor, 0x6002) == 16 + point, "Wrong PR command");
                int routes = 0;
                for (const auto& entry : ctx.audit.entries()) if (entry.level == "ROUTE") {
                    ++routes;
                    check(entry.data["device"] == motor && entry.data["port"] == registry["devices"][motor]["port"] &&
                          entry.data["address"] == registry["devices"][motor]["address"], "Wrong endpoint");
                }
                check(routes == 1, "Expected one Modbus route per trigger");
            }
        }
        for (int bad : {-1, 16}) {
            auto ctx = context();
            auto result = executor.runGroupJson(trigger, json::array({"X", bad}), ctx);
            check(result.status != ExecStatus::SUCCESS && ctx.devices.getRegister("X", 0x6002) == 0,
                  "Invalid PR point wrote to device");
        }
        {
            auto ctx = context();
            auto result = executor.runGroupJson(trigger, json::array({"UNKNOWN", 1}), ctx);
            check(result.status == ExecStatus::REFERENCE_ERROR, "Unknown device accepted");
        }
        // Same L2 default binding, and a call-level override of that binding.
        {
            auto ctx = context(); auto call = trigger["body"][1];
            call["params"] = json::array({17}); call.erase("device");
            json group = {{"type", "group"}, {"name", "missing binding"}, {"mode", "sequence"}, {"body", json::array({call})}};
            check(executor.runGroupJson(group, json::array(), ctx).status == ExecStatus::REFERENCE_ERROR,
                  "Missing device binding accepted");
        }
        for (bool override_device : {false, true}) {
            auto ctx = context(); ctx.vars.set("device", "Z");
            auto call = trigger["body"][1]; call["params"] = json::array({17});
            call.erase("device"); if (override_device) call["device"] = "X";
            json group = {{"type", "group"}, {"name", "binding"}, {"mode", "sequence"}, {"body", json::array({call})}};
            auto result = executor.runGroupJson(group, json::array(), ctx);
            check(result.status == ExecStatus::SUCCESS, result.message);
            check(ctx.devices.getRegister(override_device ? "X" : "Z", 0x6002) == 17, "Binding precedence");
        }
        // Unknown state triggers all axes once; subsequent moves compare full XYZ tuples.
        const auto move = read(root + "/L3_group/move_production_point.group.json");
        auto trigger_devices = [](const ExecContext& ctx) {
            std::vector<std::string> result;
            for (const auto& e : ctx.audit.entries()) if (e.level == "ROUTE" &&
                e.data["request"].get<std::string>().find("06 60 02") == 0)
                result.push_back(e.data["device"].get<std::string>());
            return result;
        };
        auto ctx = context();
        int last_x = -1, last_y = -1, last_z = -1;
        size_t index = 0;
        for (const auto& point : variables["production_points"]) {
            ctx.audit.clear();
            auto result = executor.runGroupJson(move, json::array({index}), ctx);
            check(result.status == ExecStatus::SUCCESS, result.message);
            std::vector<std::string> expected;
            if (point["x"] != last_x) expected.push_back("X");
            if (point["y"] != last_y) expected.insert(expected.end(), {"Y1", "Y2"});
            if (point["z"] != last_z) expected.push_back("Z");
            check(trigger_devices(ctx) == expected, "Only changed axes may be triggered");
            last_x = point["x"].get<int>(); last_y = point["y"].get<int>(); last_z = point["z"].get<int>();
            for (const auto& axis : {"x", "y", "z"})
                check(ctx.vars.resolveValue("${last_" + std::string(axis) + "_pr}") == point[axis], "Issued-point cache differs");
            // Repeating the complete point still waits/refreshes but emits no PR trigger.
            ctx.audit.clear();
            result = executor.runGroupJson(move, json::array({index++}), ctx);
            check(result.status == ExecStatus::SUCCESS && trigger_devices(ctx).empty(), "Same point retriggered");
        }
        {
            auto test = context();
            auto points = json::array({{{"x", 3}, {"y", 2}, {"z", 5}, {"wait_ms", 2000}},
                                       {{"x", 3}, {"y", 2}, {"z", 6}, {"wait_ms", 2000}},
                                       {{"x", 3}, {"y", 4}, {"z", 6}, {"wait_ms", 2000}}});
            test.vars.set("production_points", points);
            check(executor.runGroupJson(move, json::array({0}), test).status == ExecStatus::SUCCESS, "Initial tuple");
            test.audit.clear();
            check(executor.runGroupJson(move, json::array({1}), test).status == ExecStatus::SUCCESS, "Z-only tuple");
            check(trigger_devices(test) == std::vector<std::string>{"Z"}, "Z-only change must skip X/Y");
            test.audit.clear();
            test.devices.setOnline("Y2", false);
            executor.runGroupJson(move, json::array({2}), test);
            check(test.vars.resolveValue("${last_y_pr}") == -1, "Partial Y failure cached a successful target");
            test.devices.setOnline("Y2", true); test.audit.clear();
            check(executor.runGroupJson(move, json::array({2}), test).status == ExecStatus::SUCCESS, "Y retry");
            check(trigger_devices(test) == std::vector<std::string>({"Y1", "Y2"}) &&
                  test.vars.resolveValue("${last_y_pr}") == 4, "Y pair must retry together");
        }
        for (int bad : {-1, 101, 999}) {
            auto test = context();
            auto result = executor.runGroupJson(move, json::array({bad}), test);
            check(result.status != ExecStatus::SUCCESS && trigger_devices(test).empty(), "Invalid point index accepted");
        }
        const auto poll = read(root + "/L3_group/poll_sensor_until_ready.group.json");
        auto poll_params = json::array({"L2_node/all_nodes.json/read_fc04_reg0038.r_b_poll_ready",
                                       "LevelSensor", 50, 8000, 162, "LEVEL_TIMEOUT", "Level not reached"});
        {
            auto test = context(); test.devices.setRegister("LevelSensor", 0x0038, 1);
            auto result = executor.runGroupJson(poll, poll_params, test);
            check(result.status == ExecStatus::SUCCESS && test.time_ms == 5, "Ready poll must not wait");
        }
        {
            auto test = context();
            test.devices.setBehavior("LevelSensor", [&](uint64_t dt) {
                if (test.time_ms + dt >= 50) test.devices.setRegister("LevelSensor", 0x0038, 1);
            });
            auto result = executor.runGroupJson(poll, poll_params, test);
            check(result.status == ExecStatus::SUCCESS && test.time_ms == 60, "Failed poll waits once then succeeds");
        }
        {
            auto test = context(); auto params = poll_params;
            params[3] = 80; params[4] = 3;
            auto result = executor.runGroupJson(poll, params, test);
            check(result.status == ExecStatus::TIMEOUT, "Polling deadline must remain effective");
            bool popup = false;
            for (const auto& entry : test.audit.entries())
                if (entry.message == "HMI popup: Level not reached" && entry.data.is_object() &&
                    entry.data.value("error_code", "") == "LEVEL_TIMEOUT") popup = true;
            check(popup, "Parameterized timeout diagnosis was lost");
        }
        {
            auto test = context(); auto params = poll_params; params[0] = "missing_node";
            check(executor.runGroupJson(poll, params, test).status == ExecStatus::REFERENCE_ERROR,
                  "Unregistered dynamic node accepted");
            params = poll_params; params[4] = 0;
            check(executor.runGroupJson(poll, params, test).status == ExecStatus::FAILED, "Invalid loop budget accepted");
        }
        std::cout << "PASS: all device routes, PR bounds, XYZ change detection, repeat skip, Y failure/retry, reusable polling, " << index << " points\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
