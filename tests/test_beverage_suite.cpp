// Compatibility smoke test on the repository's real executor. Devices are
// deliberately instant-ready here; timed physical-model comparison is Python.
#include "industrial_config_engine/executor.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace industrial_config_engine;
using json = nlohmann::json;
static json read(const std::string& path) { std::ifstream in(path); json j; in >> j; return j; }
static void check(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Pass examples/beverage_suite");
        std::string root = argv[1], error;
        auto registry = read(root + "/device_registry.json"), variables = read(root + "/system_variables.json");
        Executor executor;
        check(executor.addRoot(root + "/layered") && executor.loadLayers(), "Load shared layers");
        json results = json::array();
        for (const auto& recipe : variables["recipes"]) {
            ExecContext ctx; ctx.verbose = false;
            check(ctx.devices.configure(registry, error), error);
            for (auto it = variables.begin(); it != variables.end(); ++it) ctx.vars.set(it.key(), it.value());
            for (auto it = registry["devices"].begin(); it != registry["devices"].end(); ++it) {
                const auto device = it.key();
                ctx.devices.setRegister(device, 0, 1);
                ctx.devices.setRegister(device, 1, 1);
                ctx.devices.setBehavior(device, [&, device](uint64_t) {
                    auto command = ctx.devices.getRegister(device, 0x6002);
                    if (command >= 16 && command <= 31) ctx.devices.setRegister(device, 0x602C, (command - 16) * 1000);
                    if (ctx.devices.getCoil(device, 0))
                        ctx.devices.setRegister(device, 0x0012, ctx.devices.getRegister(device, 0x0010));
                });
            }
            const std::string id = recipe["id"].get<std::string>();
            auto result = executor.runFlowFile(root + "/layered/L4_flow/" + id + ".json", ctx);
            check(result.status == ExecStatus::SUCCESS, id + ": " + result.message);
            check(ctx.vars.get("completed") && ctx.vars.get("completed")->get<double>() == 12, "Two batches of six required");
            check(ctx.vars.get("dispensed_ml") && *ctx.vars.get("dispensed_ml") == recipe["volume_ml"], "Volume differs");
            int selected_head_starts = 0, wrong_head_starts = 0, infeed = 0, closure = 0, outfeed = 0, routes = 0;
            for (const auto& event : ctx.audit.entries()) if (event.level == "ROUTE") {
                ++routes;
                const auto device = event.data["device"].get<std::string>();
                const auto request = event.data["request"].get<std::string>();
                if (device.find("Fill") != std::string::npos && request == "05 00 00 FF 00") {
                    if (device == recipe["fill_device"].get<std::string>()) ++selected_head_starts;
                    else ++wrong_head_starts;
                }
                if (request.find("06 60 02") == 0) {
                    if (device == "Infeed") ++infeed;
                    if (device == "Closure") ++closure;
                    if (device == "Outfeed") ++outfeed;
                }
            }
            check(selected_head_starts == 12 && wrong_head_starts == 0, "Wrong flavor head use");
            check(infeed == 12 && closure == 12 && outfeed == 12, "Missing repeated auxiliary-motor cycle");
            results.push_back({{"recipe", id}, {"status", "SUCCESS"}, {"completed", 12}, {"modbus_routes", routes}});
        }
        std::cout << json({{"profile", "instant-ready C++ executor compatibility test"}, {"recipes", results}}).dump(2) << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
