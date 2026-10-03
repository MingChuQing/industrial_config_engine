// JSON adapter for the real parser, validator and virtual executor.
// This program never opens a serial port or sends commands to physical devices.
#include "industrial_config_engine/executor.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace industrial_config_engine;
using json = nlohmann::json;

namespace {

// Keep loader diagnostics in the machine-readable result, not mixed with JSON.
struct CaptureStreams {
    std::ostringstream output;
    std::streambuf* oldOut = std::cout.rdbuf(output.rdbuf());
    std::streambuf* oldErr = std::cerr.rdbuf(output.rdbuf());
    ~CaptureStreams() {
        std::cout.rdbuf(oldOut);
        std::cerr.rdbuf(oldErr);
    }
};

void predicate(json& result, const std::string& name, const std::function<bool()>& check) {
    try {
        const bool accepted = check();
        result["checks"][name] = accepted;
        if (!accepted) result["errors"].push_back(name + " returned false");
    } catch (const std::exception& error) {
        result["checks"][name] = false;
        result["errors"].push_back(name + ": " + error.what());
    }
}

json structuralCheck(const std::string& kind, const json& configuration) {
    json result = {{"accepted", false}, {"errors", json::array()},
                   {"checks", json::object()},
                   {"scope", "Existing L3Group/L4Flow parser and validator APIs; not a full external schema or reference resolver."}};
    if (kind == "group") {
        L3Group group;
        const bool loaded = group.loadFromJson(configuration);
        result["parsed"] = group.isInitialized();
        result["accepted"] = loaded;
        if (!group.isInitialized()) {
            result["errors"].push_back("L3Group::loadFromJson failed during parsing (the API suppresses the original exception)");
        } else {
            predicate(result, "L3Group::validate", [&] { return group.validate(); });
            predicate(result, "L3Group::validateBody", [&] { return group.validateBody(); });
            predicate(result, "L3Group::validateLoop", [&] { return group.validateLoop(); });
            predicate(result, "L3Group::validateCondition", [&] { return group.validateCondition(); });
            predicate(result, "L3Group::validateCases", [&] { return group.validateCases(); });
            predicate(result, "L3Group::validateMaxNodes", [&] { return group.validateMaxNodes(); });
        }
    } else if (kind == "flow") {
        L4Flow flow;
        const bool loaded = flow.loadFromJson(configuration);
        result["parsed"] = flow.isInitialized();
        result["accepted"] = loaded;
        if (!flow.isInitialized()) {
            result["errors"].push_back("L4Flow::loadFromJson failed during parsing (the API suppresses the original exception)");
        } else {
            predicate(result, "L4Flow::validate", [&] { return flow.validate(); });
            predicate(result, "L4Flow::validateBody", [&] { return flow.validateBody(); });
            predicate(result, "L4Flow::validateProfiles", [&] { return flow.validateProfiles(); });
            predicate(result, "L4Flow::validateActiveProfile", [&] { return flow.validateActiveProfile(); });
            predicate(result, "L4Flow::validateEmergencyCleanup", [&] { return flow.validateEmergencyCleanup(); });
        }
    } else {
        throw std::invalid_argument("kind must be group or flow");
    }
    return result;
}

void setupOriginalExamples(ExecContext& ctx) {
    // Same deterministic engineering-value model as demos/demo_simulator.cpp.
    ctx.devices.setOnline("default", true);
    ctx.devices.setOnline("1", true);
    ctx.devices.setRegister("default", 0x6000, 0);
    ctx.devices.setRegister("default", 512, 1);
    ctx.devices.setRegister("default", 1, 65000);
    ctx.devices.setRegister("default", 2000, 65000);
    ctx.devices.setRegister("1", 2000, 65000);
    ctx.devices.setBehavior("default", [&ctx](uint64_t) {
        if (ctx.devices.getRegister("default", 0x6000) == 1) {
            for (uint16_t address : {uint16_t(1), uint16_t(2000)}) {
                const int64_t pressure = ctx.devices.getRegister("default", address);
                ctx.devices.setRegister("default", address,
                    std::max<int64_t>(0, pressure - std::max<int64_t>(1, pressure / 40)));
            }
        }
    });
    ctx.devices.setBehavior("1", [&ctx](uint64_t) {
        if (ctx.devices.getRegister("default", 0x6000) == 1) {
            const int64_t pressure = ctx.devices.getRegister("1", 2000);
            ctx.devices.setRegister("1", 2000,
                std::max<int64_t>(0, pressure - std::max<int64_t>(1, pressure / 40)));
        }
    });
}

void setupBeverage(ExecContext& ctx, const json& registry, int64_t volume) {
    // Same instant-ready compatibility model as tests/test_beverage_full.cpp.
    // Registers hold engineering values, not IEEE-754 wire representations.
    if (!registry.is_object() || !registry.contains("devices") || !registry["devices"].is_object())
        throw std::invalid_argument("beverage profile requires device_registry.devices object");
    for (auto item = registry["devices"].begin(); item != registry["devices"].end(); ++item) {
        const auto device = item.key();
        ctx.devices.setRegister(device, 0, 1);
        ctx.devices.setRegister(device, 1, 1);
        ctx.devices.setRegister(device, 0x2203, 0);
        ctx.devices.setRegister(device, 0x1003, 1);
        ctx.devices.setBehavior(device, [&ctx, device](uint64_t) {
            const auto command = ctx.devices.getRegister(device, 0x6002);
            if (command >= 16 && command <= 31)
                ctx.devices.setRegister(device, 0x602C, (command - 16) * 1000);
            if (device.find("Fill") != std::string::npos && ctx.devices.getCoil(device, 0))
                ctx.devices.setRegister(device, 0x0012, ctx.devices.getRegister(device, 0x0010));
        });
    }
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
}

json execute(const json& request, const std::string& kind, const json& configuration) {
    ExecContext ctx;
    ctx.verbose = false;
    ctx.step_budget = request.value("step_budget", size_t(200000));
    // Automatic audit instrumentation; no human approval or agreement is measured.
    ctx.confirm_between = request.value("confirm_between", false);
    json result = {{"status", "NOT_RUN"}, {"message", ""}, {"steps", 0},
                   {"sim_time_ms", 0}, {"audit", json::array()}, {"variables", json::object()}};
    try {
        Executor executor;
        const auto roots = request.value("roots", json::array());
        if (!roots.is_array()) throw std::invalid_argument("roots must be an array");
        for (const auto& root : roots) {
            const auto path = root.get<std::string>();
            if (!executor.addRoot(path)) throw std::runtime_error("Cannot add layer root: " + path);
        }
        if (!executor.loadLayers()) throw std::runtime_error("Executor::loadLayers returned false");
        result["loaded_actions"] = executor.actions().getActionCount();
        result["loaded_nodes"] = executor.nodes().getNodeCount();
        const auto variables = request.value("variables", json::object());
        if (!variables.is_object()) throw std::invalid_argument("variables must be an object");
        for (auto item = variables.begin(); item != variables.end(); ++item)
            ctx.vars.set(item.key(), item.value());
        const auto registry = request.value("device_registry", json::object());
        if (!registry.empty()) {
            std::string error;
            if (!ctx.devices.configure(registry, error)) throw std::runtime_error(error);
        }
        const auto profile = request.value("profile", std::string("default"));
        if (profile == "beverage") setupBeverage(ctx, registry, request.value("beverage_volume_ml", int64_t(2000)));
        else if (profile == "default" || profile == "vacuum" || profile == "feeding" || profile == "leak")
            setupOriginalExamples(ctx);
        else throw std::invalid_argument("Unknown virtual device profile: " + profile);

        // Optional simulated state overrides support repeatable device fault cases.
        for (const auto& device : request.value("device_overrides", json::array())) {
            const auto name = device.at("device").get<std::string>();
            if (device.contains("online")) ctx.devices.setOnline(name, device.at("online").get<bool>());
            for (const auto& reg : device.value("registers", json::array()))
                ctx.devices.setRegister(name, reg.at("address").get<uint16_t>(), reg.at("value").get<int64_t>());
            for (const auto& coil : device.value("coils", json::array()))
                ctx.devices.setCoil(name, coil.at("address").get<uint16_t>(), coil.at("value").get<bool>());
        }
        result["status"] = "INTERNAL_ERROR";
        const ExecResult execution = kind == "group"
            ? executor.runGroupJson(configuration, request.value("params", json::array()), ctx)
            : executor.runFlowJson("defect probe", configuration, ctx);
        result["status"] = execStatusToString(execution.status);
        result["message"] = execution.message;
    } catch (const std::exception& error) {
        result["message"] = error.what();
        result["exception"] = true;
    } catch (...) {
        result["message"] = "Unknown C++ exception";
        result["exception"] = true;
    }
    // Read context counters directly; runGroupJson does not populate result counters.
    result["steps"] = ctx.steps;
    result["sim_time_ms"] = ctx.time_ms;
    for (const auto& entry : ctx.audit.entries())
        result["audit"].push_back({{"time_ms", entry.time_ms}, {"level", entry.level},
                                   {"message", entry.message}, {"data", entry.data}});
    for (const auto& entry : ctx.vars.all()) result["variables"][entry.first] = entry.second;
    result["devices"] = ctx.devices.snapshot();
    return result;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: defect_probe <request.json> <output.json>\n";
        return 2;
    }
    json result = {{"schema_version", 1}, {"request_error", nullptr},
                   {"model", "Virtual executor only; no physical device I/O or independent human review."}};
    int exitCode = 0;
    {
        CaptureStreams capture;
        try {
            auto inputPath = std::filesystem::u8path(argv[1]);
#ifdef _WIN32
            // Python's Windows environment preserves Unicode paths even though
            // main(argc, argv) may receive arguments in the system code page.
            if (const wchar_t* path = _wgetenv(L"ICE_DEFECT_REQUEST"))
                inputPath = std::filesystem::path(path);
#endif
            std::ifstream input(inputPath, std::ios::binary);
            if (!input.good()) throw std::runtime_error("Cannot open request file");
            json request;
            input >> request;
            const auto kind = request.at("kind").get<std::string>();
            const auto& configuration = request.at("configuration");
            result["structural"] = structuralCheck(kind, configuration);
            result["simulation"] = execute(request, kind, configuration);
        } catch (const std::exception& error) {
            result["request_error"] = error.what();
            exitCode = 2;
        } catch (...) {
            result["request_error"] = "Unknown C++ exception";
            exitCode = 2;
        }
        result["diagnostics"] = capture.output.str();
    }
    try {
        auto outputPath = std::filesystem::u8path(argv[2]);
#ifdef _WIN32
        if (const wchar_t* path = _wgetenv(L"ICE_DEFECT_RESULT"))
            outputPath = std::filesystem::path(path);
#endif
        std::ofstream output(outputPath, std::ios::binary);
        if (!output.good()) throw std::runtime_error("Cannot open output file");
        output << result.dump(2) << '\n';
        if (!output.good()) throw std::runtime_error("Cannot write output file");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
    // A rejected configuration is a valid observation, not a probe process failure.
    return exitCode;
}
