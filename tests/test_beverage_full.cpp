// Runs the full twelve-stage recipe configurations through the real executor.
// This compatibility model makes devices immediately ready. The executor stores
// engineering-value integers; it does not decode little-endian float bytes or
// render UI data. Timed protocol/UI equivalence is verified separately in Python.
#include "industrial_config_engine/executor.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace industrial_config_engine;
using json = nlohmann::json;

static void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

static json readJson(const std::string& path) {
    std::ifstream in(path);
    check(in.good(), "Cannot read " + path);
    json value;
    in >> value;
    return value;
}

static int stageNumber(const json& data) {
    if (!data.is_object() || !data.contains("template") || !data["template"].is_string()) return 0;
    const auto path = data["template"].get<std::string>();
    const std::string prefix = "L3_group/step_";
    if (path.find(prefix) != 0 || path.size() < prefix.size() + 3) return 0;
    return std::stoi(path.substr(prefix.size(), 2));
}

int main(int argc, char** argv) {
    try {
        check(argc == 2 || argc == 3, "Pass examples/beverage_full/example4 or example5 [report.json]");
        const std::string root = argv[1];
        const auto registry = readJson(root + "/device_registry.json");
        const auto variables = readJson(root + "/system_variables.json");
        check(variables["recipes"].size() == 6 || variables["recipes"].size() == 9,
              "Expected six baseline recipes or nine extended recipes");
        Executor executor;
        check(executor.addRoot(root + "/layered") && executor.loadLayers(), "Cannot load shared layers");
        json negativeChecks = json::array();
        if (variables["recipes"].size() == 6) {
            ExecContext unavailable;
            unavailable.verbose = false;
            std::string error;
            check(unavailable.devices.configure(registry, error), error);
            for (auto it = variables.begin(); it != variables.end(); ++it)
                unavailable.vars.set(it.key(), it.value());
            const auto rejected = executor.runGroupFile(root + "/layered/L3_group/run_recipe.group.json",
                                                       json::array({0, 2}), unavailable);
            int routes = 0;
            for (const auto& event : unavailable.audit.entries())
                if (event.level == "ROUTE") ++routes;
            check(rejected.status != ExecStatus::SUCCESS && routes == 0,
                  std::string("Unavailable baseline specification must fail before any device I/O: status=") +
                  execStatusToString(rejected.status) + ", routes=" + std::to_string(routes) +
                  ", message=" + rejected.message);
            negativeChecks.push_back({{"case", "baseline flavor 0, specification 2 unavailable"},
                                      {"status", "PASS"}, {"execution_status", execStatusToString(rejected.status)},
                                      {"message", rejected.message}, {"modbus_routes", routes}});
        }
        json results = json::array();
        for (const auto& recipe : variables["recipes"]) {
            const auto id = recipe["id"].get<std::string>();
            const auto fillDevice = recipe["fill_device"].get<std::string>();
            const auto volume = recipe["volume_ml"].get<int64_t>();
            const auto fillTime = recipe["fill_time_ms"].get<int64_t>();
            ExecContext ctx;
            ctx.verbose = false;
            // The executor's automatic confirmation audit records group calls.
            // It causes no human prompt and lets us check actual stage traversal.
            ctx.confirm_between = true;
            std::string error;
            check(ctx.devices.configure(registry, error), error);
            for (auto it = variables.begin(); it != variables.end(); ++it) ctx.vars.set(it.key(), it.value());
            for (auto it = registry["devices"].begin(); it != registry["devices"].end(); ++it) {
                const auto device = it.key();
                ctx.devices.setRegister(device, 0, 1);
                ctx.devices.setRegister(device, 1, 1);
                ctx.devices.setRegister(device, 0x2203, 0);
                ctx.devices.setRegister(device, 0x1003, 1);
                ctx.devices.setBehavior(device, [&, device](uint64_t) {
                    const auto command = ctx.devices.getRegister(device, 0x6002);
                    if (command >= 16 && command <= 31)
                        ctx.devices.setRegister(device, 0x602C, (command - 16) * 1000);
                    if (device.find("Fill") != std::string::npos && ctx.devices.getCoil(device, 0))
                        ctx.devices.setRegister(device, 0x0012, ctx.devices.getRegister(device, 0x0010));
                });
            }
            // Float-typed protocol reads return these engineering-value integers
            // in the current executor; these are not IEEE-754 register encodings.
            ctx.devices.setRegister("PressureSensor", 0x0010, 10);
            ctx.devices.setRegister("PressureSensor", 0x0012, 10);
            ctx.devices.setRegister("FlowMeter", 0x0000, volume);
            ctx.devices.setRegister("Torque", 0x0030, 3);
            for (uint16_t address = 0x0034; address <= 0x0037; ++address)
                ctx.devices.setRegister("BottleSensor", address, 1);
            for (uint16_t address = 0x0040; address <= 0x0047; ++address)
                ctx.devices.setRegister("BottleSensor", address, 1);
            for (uint16_t address = 0x0038; address <= 0x003B; ++address)
                ctx.devices.setRegister("LevelSensor", address, 1);

            const auto result = executor.runFlowFile(root + "/layered/L4_flow/" + id + ".json", ctx);
            check(result.status == ExecStatus::SUCCESS, id + ": " + result.message);
            check(ctx.vars.get("completed") && ctx.vars.get("completed")->get<double>() == 12,
                  id + ": two batches of six required");
            check(ctx.vars.get("dispensed_ml") && *ctx.vars.get("dispensed_ml") == recipe["volume_ml"],
                  id + ": dispensed volume differs");

            int selectedStarts = 0, wrongStarts = 0, routes = 0, volumeWrites = 0, timeWrites = 0;
            std::map<std::string, int> auxiliaryCycles, diagnosticReads;
            std::map<int, int> stageCounts;
            std::vector<int> stages, expectedStages{1};
            uint64_t cipStart = 0, standbyStart = 0;
            for (const auto& event : ctx.audit.entries()) {
                check(event.level != "ERROR", id + ": " + event.message);
                check(event.level != "WARN", id + ": " + event.message);
                if (event.level == "OPERATOR_CONFIRM") {
                    const int stage = stageNumber(event.data);
                    if (stage) {
                        stages.push_back(stage);
                        ++stageCounts[stage];
                        if (stage == 11) cipStart = event.time_ms;
                        if (stage == 12) standbyStart = event.time_ms;
                    }
                }
                if (event.level == "CHANGE" && event.message == "register write" &&
                    event.data["device"] == fillDevice) {
                    const auto address = event.data["register"].get<int>();
                    if (address == 0x0010) {
                        check(event.data["value"] == volume, id + ": wrong volume command");
                        ++volumeWrites;
                    }
                    if (address == 0x0011) {
                        check(event.data["value"] == fillTime, id + ": wrong fill duration command");
                        ++timeWrites;
                    }
                }
                if (event.level != "ROUTE") continue;
                ++routes;
                const auto device = event.data["device"].get<std::string>();
                const auto request = event.data["request"].get<std::string>();
                if (device.find("Fill") != std::string::npos && request == "05 00 00 FF 00") {
                    if (device == fillDevice) ++selectedStarts;
                    else ++wrongStarts;
                }
                if (request.find("06 60 02") == 0) ++auxiliaryCycles[device];
                if ((request.find("03 ") == 0 || request.find("04 ") == 0) &&
                    (device == "PressureSensor" || device == "FlowMeter" || device == "Torque"))
                    ++diagnosticReads[device];
            }
            for (int unit = 0; unit < 12; ++unit)
                for (int stage = 2; stage <= 10; ++stage) expectedStages.push_back(stage);
            expectedStages.push_back(11);
            expectedStages.push_back(12);
            check(stages == expectedStages, id + ": twelve-stage order or repetition differs");
            check(selectedStarts == 12 && wrongStarts == 0, id + ": wrong flavor head use");
            check(volumeWrites == 12 && timeWrites == 12, id + ": missing per-container filling parameters");
            for (const std::string device : {"Infeed", "Closure", "Outfeed"})
                check(auxiliaryCycles[device] == 12, id + ": missing " + device + " cycle");
            check(diagnosticReads["PressureSensor"] >= 61 && diagnosticReads["FlowMeter"] >= 24 &&
                  diagnosticReads["Torque"] >= 24, id + ": restored diagnostics did not execute");
            check(standbyStart >= cipStart + 170000 && standbyStart < cipStart + 171000,
                  id + ": expected 120s + 30s + 20s virtual CIP waits");
            json stageCountsJson = json::object();
            for (const auto& item : stageCounts)
                stageCountsJson[(item.first < 10 ? "0" : "") + std::to_string(item.first)] = item.second;
            results.push_back({{"recipe", id}, {"status", "SUCCESS"}, {"completed", 12},
                               {"modbus_routes", routes}, {"stage_group_counts", stageCountsJson},
                               {"virtual_time_ms", ctx.time_ms}, {"cip_virtual_ms", standbyStart - cipStart},
                               {"fill_time_writes", timeWrites}, {"diagnostic_reads", diagnosticReads}});
        }
        const json report = {
            {"profile", "instant-ready C++ executor compatibility test"},
            {"limitations", {"Engineering-value integers stand in for LE float protocol reads; byte decoding is not tested.",
                             "Stage counts are executed group calls; UI target/value rendering is not tested.",
                             "Device completion is immediate and wait durations advance virtual time only."}},
            {"negative_checks", negativeChecks},
            {"recipes", results}
        };
        if (argc == 3) {
            std::ofstream out(argv[2], std::ios::binary);
            check(out.good(), std::string("Cannot write report ") + argv[2]);
            out << report.dump(2) << '\n';
        }
        std::cout << report.dump(2) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
