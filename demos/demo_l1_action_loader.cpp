// demos/demo_l1_loader.cpp
// demos/demo_l1_loader.cpp
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "industrial_config_engine/l1_action.hpp"
#include "industrial_config_engine/action_loader.hpp"

#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

using namespace industrial_config_engine;
namespace fs = std::filesystem;

// ============================================================
// color output helper (Windows console)
// ============================================================
#ifdef _WIN32
void setConsoleColor(int color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
}

void initConsole() {
    // set console code page to UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // enable ANSI escape sequence support (Windows 10+)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }

    // remove this line - it causes assertion failure
    // _setmode(_fileno(stdout), _O_U8TEXT);

    // use this alternative - set standard streams to UTF-8
    // no _setmode needed, SetConsoleOutputCP is enough
}
#else
void setConsoleColor(int color) {}
void initConsole() {}
#endif

enum ConsoleColor {
    COLOR_RESET = 7,
    COLOR_RED = 12,
    COLOR_GREEN = 10,
    COLOR_YELLOW = 14,
    COLOR_BLUE = 9,
    COLOR_CYAN = 11,
    COLOR_MAGENTA = 13,
    COLOR_WHITE = 15
};

// ============================================================
// print helper functions
// ============================================================

void printHeader(const std::string& title) {
    std::cout << "\n";
    setConsoleColor(COLOR_CYAN);
    std::cout << "+" << std::string(78, '-') << "+" << std::endl;
    std::cout << "| " << std::left << std::setw(76) << title << "|" << std::endl;
    std::cout << "+" << std::string(78, '-') << "+" << std::endl;
    setConsoleColor(COLOR_RESET);
}

void printSubHeader(const std::string& title) {
    setConsoleColor(COLOR_YELLOW);
    std::cout << "\n--- " << title << " ---" << std::endl;
    std::cout << std::string(title.length() + 8, '-') << std::endl;
    setConsoleColor(COLOR_RESET);
}

void printSuccess(const std::string& msg) {
    setConsoleColor(COLOR_GREEN);
    std::cout << "  [OK] " << msg << std::endl;
    setConsoleColor(COLOR_RESET);
}

void printError(const std::string& msg) {
    setConsoleColor(COLOR_RED);
    std::cout << "  [ERR] " << msg << std::endl;
    setConsoleColor(COLOR_RESET);
}

void printWarning(const std::string& msg) {
    setConsoleColor(COLOR_YELLOW);
    std::cout << "  [WARN] " << msg << std::endl;
    setConsoleColor(COLOR_RESET);
}

void printInfo(const std::string& msg) {
    setConsoleColor(COLOR_BLUE);
    std::cout << "  [INFO] " << msg << std::endl;
    setConsoleColor(COLOR_RESET);
}

// ============================================================
// check if valid L1 Action file
// ============================================================

bool isL1ActionFile(const std::string& filepath) {
    try {
        std::string ext = fs::path(filepath).extension().string();
        if (ext != ".json" && ext != ".JSON") {
            return false;
        }

        std::string filename = fs::path(filepath).filename().string();
        if (filename.find("action_bundle") != std::string::npos ||
            filename.find("all_") == 0 ||
            filename == "actions.json") {
            return false;
        }

        std::string basename = fs::path(filepath).stem().string();
        auto sig = L1Action::parseSignatureFromFilename(basename);

        if (!sig.name.empty()) {
            return true;
        }

        std::ifstream file(filepath);
        if (!file.is_open()) {
            return false;
        }

        nlohmann::json json;
        try {
            file >> json;
        }
        catch (...) {
            return false;
        }

        if (json.contains("type") && json["type"].is_string()) {
            std::string type = json["type"].get<std::string>();
            std::vector<std::string> action_types = {
                "modbus_write_verify", "modbus_read_cache", "modbus_read_check",
                "wait", "calculate", "calculate_check",
                "set_variable", "read_variable",
                "ui_action", "user_decision",
                "log", "popup", "script_exec"
            };
            for (const auto& t : action_types) {
                if (type == t) {
                    return true;
                }
            }
        }

        return false;
    }
    catch (const std::exception&) {
        return false;
    }
}

// ============================================================
// recursively find L1 Action files
// ============================================================

std::vector<std::string> findL1ActionFiles(const std::string& directory) {
    std::vector<std::string> files;

    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        printError("Directory does not exist: " + directory);
        return files;
    }

    try {
        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (!entry.is_regular_file()) continue;

            std::string filepath = entry.path().string();

            if (isL1ActionFile(filepath)) {
                files.push_back(filepath);
            }
        }
    }
    catch (const std::exception& e) {
        printError("Failed to traverse directory: " + std::string(e.what()));
    }

    return files;
}

// ============================================================
// load and show all L1 Actions
// ============================================================

bool loadAndDisplayActions(const std::string& directory, bool verbose = true) {
    printHeader("L1 Action Loader");
    printInfo("Scanning directory: " + directory);

    auto files = findL1ActionFiles(directory);

    if (files.empty()) {
        printWarning("No L1 Action files found");
        return false;
    }

    printSuccess("Found " + std::to_string(files.size()) + " L1 Action files");

    ActionLoader loader;
    int loaded_count = 0;
    int error_count = 0;

    loader.setErrorCallback([](const std::string& path, const std::string& error) {
        printError("Load failed: " + path + " - " + error);
        });

    for (const auto& file : files) {
        if (verbose) {
            std::cout << "\n  Loading: " << file << std::endl;
        }

        if (loader.loadSingleFile(file)) {
            loaded_count++;
            if (verbose) {
                printSuccess("  Loaded successfully");
            }
        }
        else {
            error_count++;
            if (verbose) {
                printError("  Load failed");
            }
        }
    }

    printSubHeader("Load Statistics");
    std::cout << "  Total files: " << files.size() << std::endl;
    std::cout << "  Loaded: " << loaded_count << std::endl;
    std::cout << "  Failed: " << error_count << std::endl;
    std::cout << "  Total Actions: " << loader.getActionCount() << std::endl;

    if (loader.getActionCount() == 0) {
        printWarning("No Actions loaded");
        return false;
    }

    printSubHeader("Action List");
    auto keys = loader.getActionKeys();

    std::unordered_map<std::string, int> type_stats;
    for (const auto& key : keys) {
        const auto* action = loader.getAction(key);
        if (action) {
            type_stats[action->getType()]++;
        }
    }

    std::cout << "\n  Statistics by type:" << std::endl;
    for (const auto& [type, count] : type_stats) {
        std::cout << "    " << type << ": " << count << " actions" << std::endl;
    }

    // ============================================================
    // directly use L1Action::print() for details
    // ============================================================
    printSubHeader("Action Details");
    for (const auto& key : keys) {
        const auto* action = loader.getAction(key);
        if (action) {
            std::cout << "\n[Key: " << key << "]" << std::endl;
            action->print(std::cout);  // directly use L1Action print method
            std::cout << std::endl;
        }
    }

    return true;
}

// ============================================================
// find default directory
// ============================================================

std::string findDefaultDirectory() {
    std::string exe_path;
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    exe_path = buffer;
    exe_path = exe_path.substr(0, exe_path.find_last_of("\\/"));
#else
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1) {
        buffer[len] = '\0';
        exe_path = buffer;
        exe_path = exe_path.substr(0, exe_path.find_last_of("/"));
    }
#endif

    fs::path base_path = fs::path(exe_path);

    std::vector<fs::path> search_paths;
    search_paths.push_back(base_path);

    for (int i = 0; i < 3; ++i) {
        base_path = base_path.parent_path();
        if (base_path.empty()) break;
        search_paths.push_back(base_path);
    }

    std::vector<std::string> possible_paths = {
        "examples/config_examples/L1_action",
        "examples/L1_action",
        "L1_action",
        "../examples/L1_action",
        "../L1_action"
    };

    for (const auto& base : search_paths) {
        for (const auto& rel_path : possible_paths) {
            fs::path full_path = base / rel_path;
            if (fs::exists(full_path) && fs::is_directory(full_path)) {
                return full_path.string();
            }
        }
    }

    return fs::current_path().string();
}

// ============================================================
// print usage help
// ============================================================

void printUsage(const std::string& program_name) {
    std::cout << "Usage: " << program_name << " [options] [directory]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --help     Show this help message" << std::endl;
    std::cout << "  -v, --verbose  Show detailed loading information" << std::endl;
    std::cout << "  -q, --quiet    Quiet mode (statistics only)" << std::endl;
    std::cout << std::endl;
    std::cout << "Arguments:" << std::endl;
    std::cout << "  directory      Directory to scan (default: auto-find L1_action)" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << "                          # Auto-find L1_action directory" << std::endl;
    std::cout << "  " << program_name << " ./L1_action              # Scan specified directory" << std::endl;
    std::cout << "  " << program_name << " -v ./L1_action           # Verbose mode" << std::endl;
    std::cout << "  " << program_name << " -q ./L1_action           # Quiet mode" << std::endl;
}

// ============================================================
// main function
// ============================================================

int main(int argc, char* argv[]) {
    initConsole();

    std::string directory;
    bool verbose = true;
    bool quiet = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
            quiet = false;
        }
        else if (arg == "-q" || arg == "--quiet") {
            quiet = true;
            verbose = false;
        }
        else if (arg[0] != '-') {
            directory = arg;
        }
        else {
            printError("Unknown option: " + arg);
            printUsage(argv[0]);
            return 1;
        }
    }

    if (directory.empty()) {
        directory = findDefaultDirectory();
        if (directory.empty() || !fs::exists(directory)) {
            printError("L1_action directory not found, please specify a directory");
            printUsage(argv[0]);
            return 1;
        }
        printInfo("Auto-found L1_action directory: " + directory);
    }

    if (!fs::exists(directory)) {
        printError("Directory does not exist: " + directory);
        return 1;
    }

    if (!fs::is_directory(directory)) {
        printError("Path is not a directory: " + directory);
        return 1;
    }

    bool success = loadAndDisplayActions(directory, verbose);

    return success ? 0 : 1;
}