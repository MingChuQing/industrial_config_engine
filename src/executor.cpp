// src/executor.cpp
// 执行器与仿真运行器：虚拟设备 + 运行时变量存储 + 虚拟时钟 + 步数预算 + 审计日志
// 启动时解析与校验，运行时按数据描述执行（配套论文《数据驱动的工业控制配置语言》）
#include "industrial_config_engine/executor.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace industrial_config_engine {

    // ============================================================
    // 状态字符串
    // ============================================================
    const char* execStatusToString(ExecStatus s) {
        switch (s) {
        case ExecStatus::SUCCESS: return "SUCCESS";
        case ExecStatus::FAILED: return "FAILED";
        case ExecStatus::TIMEOUT: return "TIMEOUT";
        case ExecStatus::STEP_BUDGET_EXCEEDED: return "STEP_BUDGET_EXCEEDED";
        case ExecStatus::REFERENCE_ERROR: return "REFERENCE_ERROR";
        case ExecStatus::CONDITION_ERROR: return "CONDITION_ERROR";
        case ExecStatus::INTERNAL_ERROR: return "INTERNAL_ERROR";
        }
        return "UNKNOWN";
    }

    // ============================================================
    // 审计日志
    // ============================================================
    void AuditLog::log(uint64_t t, const std::string& level, const std::string& msg,
        const nlohmann::json& data) {
        entries_.push_back(AuditEntry{ t, level, msg, data });
    }

    std::string AuditLog::toText(size_t last_n) const {
        std::ostringstream os;
        size_t start = 0;
        if (last_n > 0 && entries_.size() > last_n) start = entries_.size() - last_n;
        for (size_t i = start; i < entries_.size(); ++i) {
            const auto& e = entries_[i];
            os << "[t=" << e.time_ms << "ms] [" << e.level << "] " << e.message;
            if (!e.data.is_null()) os << " " << e.data.dump();
            os << "\n";
        }
        return os.str();
    }

    // ============================================================
    // 虚拟设备注册表
    // ============================================================
    bool DeviceRegistry::isOnline(const std::string& device) const {
        auto it = online_.find(device);
        if (it != online_.end()) return it->second;
        return true; // 默认在线
    }

    void DeviceRegistry::setRegister(const std::string& device, uint16_t addr, int64_t value) {
        registers_[device][addr] = value;
    }

    int64_t DeviceRegistry::getRegister(const std::string& device, uint16_t addr) const {
        auto dit = registers_.find(device);
        if (dit != registers_.end()) {
            auto it = dit->second.find(addr);
            if (it != dit->second.end()) return it->second;
        }
        return 0;
    }

    void DeviceRegistry::setCoil(const std::string& device, uint16_t addr, bool on) {
        coils_[device][addr] = on;
    }

    bool DeviceRegistry::getCoil(const std::string& device, uint16_t addr) const {
        auto dit = coils_.find(device);
        if (dit != coils_.end()) {
            auto it = dit->second.find(addr);
            if (it != dit->second.end()) return it->second;
        }
        return false;
    }

    std::vector<std::string> DeviceRegistry::devices() const {
        std::vector<std::string> out;
        for (const auto& kv : online_) out.push_back(kv.first);
        for (const auto& kv : registers_) {
            if (std::find(out.begin(), out.end(), kv.first) == out.end()) out.push_back(kv.first);
        }
        return out;
    }

    void DeviceRegistry::tick(uint64_t dt_ms) {
        for (const auto& kv : behaviors_) {
            if (kv.second) kv.second(dt_ms);
        }
    }

    nlohmann::json DeviceRegistry::snapshot() const {
        nlohmann::json j;
        for (const auto& d : registers_) {
            for (const auto& kv : d.second) {
                j["registers"][d.first][std::to_string(kv.first)] = kv.second;
            }
        }
        for (const auto& d : coils_) {
            for (const auto& kv : d.second) {
                j["coils"][d.first][std::to_string(kv.first)] = kv.second;
            }
        }
        for (const auto& kv : online_) {
            j["online"][kv.first] = kv.second;
        }
        return j;
    }

    // ============================================================
    // 运行时变量存储
    // ============================================================
    const nlohmann::json* VariableStore::get(const std::string& key) const {
        auto it = vars_.find(key);
        if (it == vars_.end()) return nullptr;
        return &it->second;
    }

    std::string VariableStore::resolve(const std::string& s,
        std::vector<std::string>* missing) const {
        std::string out;
        size_t pos = 0;
        while (pos < s.size()) {
            size_t b = s.find("${", pos);
            if (b == std::string::npos) { out += s.substr(pos); break; }
            out += s.substr(pos, b - pos);
            size_t e = s.find('}', b);
            if (e == std::string::npos) { out += s.substr(b); break; }
            std::string name = s.substr(b + 2, e - b - 2);
            const nlohmann::json* v = get(name);
            if (v && v->is_string()) {
                out += v->get<std::string>();
            }
            else if (v) {
                out += v->dump();
            }
            else {
                out += s.substr(b, e - b + 1); // 保留原占位符
                if (missing) missing->push_back(name);
            }
            pos = e + 1;
        }
        return out;
    }

    nlohmann::json VariableStore::resolveValue(const nlohmann::json& v,
        std::vector<std::string>* missing) const {
        if (v.is_string()) {
            return nlohmann::json(resolve(v.get<std::string>(), missing));
        }
        if (v.is_array()) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& item : v) arr.push_back(resolveValue(item, missing));
            return arr;
        }
        return v;
    }

    // ============================================================
    // 静态工具
    // ============================================================
    static std::string trimStr(const std::string& s) {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
        return s.substr(a, b - a);
    }

    static int64_t parseHexToken(const std::string& tok, bool& ok) {
        std::string t = trimStr(tok);
        if (t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t = t.substr(2);
        if (t.empty()) { ok = false; return 0; }
        int64_t v = 0;
        for (char c : t) {
            int d = -1;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            if (d < 0) { ok = false; return 0; }
            v = v * 16 + d;
        }
        ok = true;
        return v;
    }

    std::string Executor::formatArgHex(const nlohmann::json& v, int width_bytes) {
        if (v.is_string()) return v.get<std::string>();
        int64_t n = v.is_number_integer() ? v.get<int64_t>() : static_cast<int64_t>(v.get<double>());
        if (width_bytes <= 0) {
            width_bytes = (n <= 0xFF) ? 1 : (n <= 0xFFFF) ? 2 : 4;
        }
        std::ostringstream os;
        for (int i = width_bytes - 1; i >= 0; --i) {
            int byte = static_cast<int>((static_cast<uint64_t>(n) >> (8 * i)) & 0xFF);
            os << (char)("0123456789ABCDEF"[byte >> 4]);
            os << (char)("0123456789ABCDEF"[byte & 0x0F]);
            if (i > 0) os << ' ';
        }
        return os.str();
    }

    std::string Executor::formatArg(const nlohmann::json& v, const std::string& type_name) {
        int tw = 2;   // 按参数声明类型的字节宽度
        if (type_name == "u8" || type_name == "i8") tw = 1;
        else if (type_name == "u32" || type_name == "i32") tw = 4;

        if (type_name == "f" || type_name == "float" || type_name == "double") {
            if (v.is_number_float()) return std::to_string(v.get<double>());
            return v.is_string() ? v.get<std::string>() : v.dump();
        }
        if (type_name == "s" || type_name == "string") {
            return v.is_string() ? v.get<std::string>() : v.dump();
        }
        // 数值参数：${变量} 解析后可能变成纯数字字符串，也按数值十六进制格式化
        if (v.is_string()) {
            const std::string& s = v.get<std::string>();
            bool all_digits = !s.empty() &&
                std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
            if (!all_digits) return s;
            int64_t n = std::stoll(s);
            int vw = (n <= 0xFF) ? 1 : (n <= 0xFFFF) ? 2 : 4;
            return formatArgHex(nlohmann::json(n), std::max(tw, vw));
        }
        int64_t n = v.is_number_integer() ? v.get<int64_t>() : static_cast<int64_t>(v.get<double>());
        int vw = (n <= 0xFF) ? 1 : (n <= 0xFFFF) ? 2 : 4;
        return formatArgHex(v, std::max(tw, vw));
    }

    std::string Executor::jsonToStr(const nlohmann::json& v) {
        if (v.is_null()) return "null";
        if (v.is_string()) return v.get<std::string>();
        if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
        return v.dump();
    }

    // ============================================================
    // 条件求值
    // ============================================================
    bool Executor::compareValues(const nlohmann::json& lhs, const nlohmann::json& rhs,
        const std::string& op, bool& out) {
        bool numeric = lhs.is_number() && rhs.is_number();
        if (op == "eq" || op == "==") {
            if (numeric) out = (lhs.get<double>() == rhs.get<double>());
            else out = (jsonToStr(lhs) == jsonToStr(rhs));
            return true;
        }
        if (op == "ne" || op == "!=") {
            if (numeric) out = (lhs.get<double>() != rhs.get<double>());
            else out = (jsonToStr(lhs) != jsonToStr(rhs));
            return true;
        }
        if (!numeric) return false;
        double a = lhs.get<double>(), b = rhs.get<double>();
        if (op == "lt" || op == "<") out = (a < b);
        else if (op == "le" || op == "<=") out = (a <= b);
        else if (op == "gt" || op == ">") out = (a > b);
        else if (op == "ge" || op == ">=") out = (a >= b);
        else return false;
        return true;
    }

    bool Executor::evalExpression(const std::string& expr_in, VariableStore& vars,
        std::string& err) {
        std::string expr = trimStr(expr_in);
        if (expr.empty()) { err = "empty expression"; return false; }

        // 先解析占位符（变量值若为字符串，原样嵌入后按引号/字面量处理）
        std::vector<std::string> missing;
        expr = vars.resolve(expr, &missing);
        if (!missing.empty()) {
            err = "expression references undefined variable: " + missing.front();
            return false;
        }

        // 处理 && 与 ||（自顶向下拆分，注意引号内不拆分）
        auto splitTopLevel = [&](const std::string& s, const std::string& op) -> std::vector<std::string> {
            std::vector<std::string> parts;
            size_t start = 0;
            char quote = 0;
            for (size_t i = 0; i + op.size() <= s.size(); ++i) {
                char c = s[i];
                if (c == '\'' || c == '"') {
                    if (quote == 0) quote = c;
                    else if (quote == c) quote = 0;
                }
                if (quote == 0 && s.compare(i, op.size(), op) == 0) {
                    parts.push_back(trimStr(s.substr(start, i - start)));
                    start = i + op.size();
                    i += op.size() - 1;
                }
            }
            parts.push_back(trimStr(s.substr(start)));
            return parts;
        };

        auto ands = splitTopLevel(expr, " && ");
        if (ands.size() > 1) {
            for (const auto& a : ands) {
                if (!evalExpression(a, vars, err)) return false;
            }
            return true;
        }
        auto ors = splitTopLevel(expr, " || ");
        if (ors.size() > 1) {
            for (const auto& o : ors) {
                if (evalExpression(o, vars, err)) return true;
            }
            return false;
        }

        // 单比较式：找比较运算符（引号外）
        struct Op { const char* s; size_t n; };
        static const Op ops[] = { {"==",2}, {"!=",2}, {">=",2}, {"<=",2}, {">",1}, {"<",1} };
        const Op* found = nullptr;
        size_t found_pos = std::string::npos;
        char quote = 0;
        for (size_t i = 0; i < expr.size(); ++i) {
            char c = expr[i];
            if (c == '\'' || c == '"') {
                if (quote == 0) quote = c;
                else if (quote == c) quote = 0;
                continue;
            }
            if (quote != 0) continue;
            for (const auto& op : ops) {
                if (expr.compare(i, op.n, op.s) == 0) { found = &op; found_pos = i; break; }
            }
            if (found) break;
        }

        std::string lhs_str, rhs_str;
        std::string op_str;
        bool negate = false;
        if (found) {
            lhs_str = trimStr(expr.substr(0, found_pos));
            rhs_str = trimStr(expr.substr(found_pos + found->n));
            op_str = found->s;
        }
        else {
            // 无运算符：布尔字面量或变量
            lhs_str = expr;
            rhs_str = "true";
            op_str = "==";
            if (!lhs_str.empty() && lhs_str[0] == '!') {
                negate = true;
                lhs_str = trimStr(lhs_str.substr(1));
            }
        }

        // 去引号 → 字面量
        auto toJson = [&](const std::string& s) -> nlohmann::json {
            std::string t = trimStr(s);
            if (t.size() >= 2 && ((t.front() == '\'' && t.back() == '\'') ||
                (t.front() == '"' && t.back() == '"'))) {
                return nlohmann::json(t.substr(1, t.size() - 2));
            }
            if (t == "true") return nlohmann::json(true);
            if (t == "false") return nlohmann::json(false);
            if (!t.empty() && (std::isdigit(static_cast<unsigned char>(t[0])) || t[0] == '-')) {
                if (t.find('.') != std::string::npos) {
                    try { return nlohmann::json(std::stod(t)); } catch (...) {}
                }
                try { return nlohmann::json(std::stoll(t)); } catch (...) {}
            }
            return nlohmann::json(t); // 字符串
        };

        bool ok = false;
        if (!compareValues(toJson(lhs_str), toJson(rhs_str), op_str, ok)) {
            err = "unsupported expression: " + expr_in;
            return false;
        }
        return negate ? !ok : ok;
    }

    bool Executor::evalCondition(const nlohmann::json& cond_json, VariableStore& vars,
        std::string& err) {
        if (cond_json.is_null()) { err = "missing condition config"; return false; }
        std::string type = cond_json.value("type", "");
        if (type == "expression") {
            std::string e = cond_json.value("expression", "");
            if (e.empty()) { err = "expression is empty"; return false; }
            return evalExpression(e, vars, err);
        }
        if (type == "compare") {
            std::string op = cond_json.value("condition", "eq");
            nlohmann::json lhs = vars.resolveValue(cond_json.value("value", nlohmann::json("")), nullptr);
            nlohmann::json rhs = vars.resolveValue(cond_json.value("source", nlohmann::json("")), nullptr);
            bool out = false;
            if (!compareValues(lhs, rhs, op, out)) { err = "unsupported condition comparison: " + op; return false; }
            return out;
        }
        if (type == "exists") {
            std::string var = cond_json.value("variable", "");
            if (var.empty()) { err = "exists condition missing variable name"; return false; }
            return vars.has(var);
        }
        err = "unknown condition type: " + type;
        return false;
    }

    // ============================================================
    // 执行器
    // ============================================================
    Executor::Executor() = default;

    bool Executor::addRoot(const std::string& root_dir) {
        roots_.push_back(root_dir);
        return true;
    }

    bool Executor::loadLayers() {
        bool ok = true;
        for (const auto& root : roots_) {
            if (!actions_.loadDirectory(root + "/L1_action", true)) ok = false;
            if (!nodes_.loadDirectory(root + "/L2_node", true)) ok = false;
        }
        return ok;
    }

    bool Executor::resolveGroupPath(const std::string& templ, std::string& out_path) const {
        for (const auto& root : roots_) {
            std::string p = root + "/" + templ;
            std::ifstream f1(p);
            if (f1.good()) { out_path = p; return true; }
            if (templ.size() < 5 || templ.substr(templ.size() - 5) != ".json") {
                std::ifstream f2(p + ".json");
                if (f2.good()) { out_path = p + ".json"; return true; }
            }
        }
        return false;
    }

    bool Executor::loadGroupJson(const std::string& templ, nlohmann::json& out_json) {
        auto it = group_cache_.find(templ);
        if (it != group_cache_.end()) { out_json = it->second; return true; }
        std::string path;
        if (!resolveGroupPath(templ, path)) return false;
        try {
            std::ifstream in(path);
            nlohmann::json j;
            in >> j;
            group_cache_[templ] = j;
            out_json = j;
            return true;
        }
        catch (...) { return false; }
    }

    void Executor::trace(ExecContext& ctx, const std::string& msg) const {
        if (!ctx.verbose) return;
        std::cout << "[t=" << ctx.time_ms << "ms] " << std::string(ctx.indent * 2, ' ') << msg << std::endl;
    }

    bool Executor::checkSteps(ExecContext& ctx, ExecResult& r) const {
        ++ctx.steps;
        if (ctx.steps > ctx.step_budget) {
            r.status = ExecStatus::STEP_BUDGET_EXCEEDED;
            r.message = "step budget exceeded (" + std::to_string(ctx.step_budget) + "), possible infinite loop";
            return false;
        }
        return true;
    }

    void Executor::advanceTime(ExecContext& ctx, uint64_t dt_ms) {
        if (dt_ms == 0) return;
        ctx.devices.tick(dt_ms);
        ctx.time_ms += dt_ms;
    }

    uint64_t Executor::timeoutValue(const nlohmann::json& v, ExecContext& ctx) const {
        if (v.is_number()) return v.get<uint64_t>();
        if (v.is_string()) {
            std::string s = trimStr(resolveStr(v.get<std::string>(), ctx));
            try { return static_cast<uint64_t>(std::stoull(s)); }
            catch (...) { return 0; }
        }
        return 0;
    }

    // 简单四则运算求值（仿真中用于 calculate 动作）：+ - * / 括号 一元正负 数字
    static bool evalArith(const std::string& expr, double& out) {
        size_t pos = 0;
        auto skipWs = [&]() {
            while (pos < expr.size() && std::isspace(static_cast<unsigned char>(expr[pos]))) ++pos;
        };
        std::function<bool(double&)> parseExpr, parseTerm, parseFactor;
        parseExpr = [&](double& r) -> bool {
            double lhs = 0;
            if (!parseTerm(lhs)) return false;
            r = lhs;
            skipWs();
            while (pos < expr.size() && (expr[pos] == '+' || expr[pos] == '-')) {
                char op = expr[pos++];
                double rhs = 0;
                if (!parseTerm(rhs)) return false;
                r = (op == '+') ? r + rhs : r - rhs;
                skipWs();
            }
            return true;
        };
        parseTerm = [&](double& r) -> bool {
            double lhs = 0;
            if (!parseFactor(lhs)) return false;
            r = lhs;
            skipWs();
            while (pos < expr.size() && (expr[pos] == '*' || expr[pos] == '/')) {
                char op = expr[pos++];
                double rhs = 0;
                if (!parseFactor(rhs)) return false;
                if (op == '*') r *= rhs;
                else {
                    if (rhs == 0) return false;
                    r /= rhs;
                }
                skipWs();
            }
            return true;
        };
        parseFactor = [&](double& r) -> bool {
            skipWs();
            if (pos >= expr.size()) return false;
            if (expr[pos] == '(') {
                ++pos;
                if (!parseExpr(r)) return false;
                skipWs();
                if (pos >= expr.size() || expr[pos] != ')') return false;
                ++pos;
                return true;
            }
            if (expr[pos] == '+' || expr[pos] == '-') {
                char sg = expr[pos++];
                if (!parseFactor(r)) return false;
                if (sg == '-') r = -r;
                return true;
            }
            size_t start = pos;
            while (pos < expr.size() &&
                (std::isdigit(static_cast<unsigned char>(expr[pos])) || expr[pos] == '.')) ++pos;
            if (start == pos) return false;
            try {
                r = std::stod(expr.substr(start, pos - start));
            }
            catch (...) { return false; }
            return true;
        };
        skipWs();
        if (!parseExpr(out)) return false;
        skipWs();
        return pos >= expr.size();
    }

    std::string Executor::resolveStr(const std::string& s, ExecContext& ctx) const {
        std::vector<std::string> missing;
        std::string r = ctx.vars.resolve(s, &missing);
        for (const auto& m : missing) {
            ctx.audit.log(ctx.time_ms, "WARN", "placeholder references undefined variable: ${" + m + "}");
        }
        return r;
    }

    nlohmann::json Executor::resolveValue(const nlohmann::json& v, ExecContext& ctx) const {
        std::vector<std::string> missing;
        nlohmann::json r = ctx.vars.resolveValue(v, &missing);
        for (const auto& m : missing) {
            ctx.audit.log(ctx.time_ms, "WARN", "parameter references undefined variable: ${" + m + "}");
        }
        return r;
    }

    // 归一化引用键：加载器 bundle 键不含 ".json"，引用串可能含 ".json"
    static std::vector<std::string> refKeyCandidates(const std::string& templ) {
        std::vector<std::string> c{ templ };
        std::string t = templ;
        size_t pos = 0;
        while ((pos = t.find(".json/", pos)) != std::string::npos) {
            t.erase(pos, 5); // 去掉 ".json"
        }
        if (t != templ) c.push_back(t);
        return c;
    }

    ExecResult Executor::execBranchItems(const nlohmann::json& branch, ExecContext& ctx) {
        ExecResult r;
        if (branch.is_null() || !branch.is_array()) return r;
        for (const auto& item : branch) {
            r = execItem(item, ctx);
            if (r.status != ExecStatus::SUCCESS) return r;
        }
        return r;
    }

    ExecResult Executor::execBody(const nlohmann::json& body, ExecContext& ctx, bool parallel) {
        ExecResult r;
        if (body.is_null() || !body.is_array()) return r;
        for (const auto& item : body) {
            if (parallel) trace(ctx, "|| (parallel)");
            r = execItem(item, ctx);
            if (r.status != ExecStatus::SUCCESS) return r;
        }
        return r;
    }

    ExecResult Executor::execItem(const nlohmann::json& item, ExecContext& ctx) {
        ExecResult r;
        if (!checkSteps(ctx, r)) return r;
        if (!item.is_object()) { r.status = ExecStatus::INTERNAL_ERROR; r.message = "invalid item"; return r; }
        std::string type = item.value("type", "");
        std::string name = item.value("name", "");

        // 操作员逐项确认（安全机制：所有更改在执行前经操作员确认，仿真中自动确认并记审计）
        if (ctx.confirm_between && (type == "node" || type == "group")) {
            ctx.audit.log(ctx.time_ms, "OPERATOR_CONFIRM",
                "step confirmed by operator: " + (name.empty() ? type : name),
                { {"template", item.contains("template") ? item["template"] : item.value("node", nlohmann::json()) } });
        }

        if (type == "node") {
            r = execNodeRef(item, ctx);
        }
        else if (type == "group") {
            r = execGroupRef(item, ctx);
        }
        else if (type == "log") {
            std::string msg = resolveStr(item.value("message", ""), ctx);
            trace(ctx, "* log: " + msg);
            ctx.audit.log(ctx.time_ms, "INFO", msg);
        }
        else if (type == "popup") {
            std::string msg = resolveStr(item.value("message", ""), ctx);
            std::string level = item.value("level", "info");
            trace(ctx, "* popup[" + level + "]: " + msg);
            ctx.audit.log(ctx.time_ms, "ERROR", "HMI popup: " + msg);
        }
        else if (type == "if") {
            std::string err;
            bool cond = evalCondition(item.value("condition", nlohmann::json()), ctx.vars, err);
            if (!err.empty()) {
                r.status = ExecStatus::CONDITION_ERROR;
                r.message = "condition evaluation failed: " + err;
                return r;
            }
            trace(ctx, std::string("> condition branch -> ") + (cond ? "then" : "else"));
            r = execBody(cond ? item.value("then", nlohmann::json()) : item.value("else", nlohmann::json()), ctx, false);
        }
        else {
            r.status = ExecStatus::INTERNAL_ERROR;
            r.message = "unknown item type: " + type;
        }
        return r;
    }

    ExecResult Executor::execGroupRef(const nlohmann::json& item, ExecContext& ctx) {
        ExecResult r;
        std::string templ = item.value("template", "");
        if (templ.empty()) {
            // 内联组定义（无 template 引用）：直接执行条目自身
            trace(ctx, "> inline group [" + item.value("name", std::string("?")) + "]");
            ctx.indent++;
            r = runGroupJson(item, item.value("params", nlohmann::json::array()), ctx);
            ctx.indent--;
            return r;
        }
        nlohmann::json g;
        if (!loadGroupJson(templ, g)) {
            r.status = ExecStatus::REFERENCE_ERROR;
            r.message = "referenced L3 template not found: " + templ;
            return r;
        }
        trace(ctx, "> group [" + g.value("name", std::string("?")) + "] <- " + templ);
        ctx.indent++;
        r = runGroupJson(g, item.value("params", nlohmann::json::array()), ctx);
        ctx.indent--;
        if (r.status != ExecStatus::SUCCESS) return r;
        return r;
    }

    ExecResult Executor::execNodeRef(const nlohmann::json& item, ExecContext& ctx) {
        ExecResult r;
        std::string templ;
        if (item.contains("template")) templ = item["template"].get<std::string>();
        else if (item.contains("node") && item["node"].is_object() && item["node"].contains("template")) {
            templ = item["node"]["template"].get<std::string>();
        }
        if (templ.empty()) { r.status = ExecStatus::FAILED; r.message = "node item missing template"; return r; }

        const L2Node* node = nullptr;
        for (const auto& key : refKeyCandidates(templ)) {
            node = nodes_.getNode(key);
            if (node) break;
        }
        if (!node) {
            r.status = ExecStatus::REFERENCE_ERROR;
            r.message = "node definition missing: " + templ;
            return r;
        }
        if (!node->validate()) {
            r.status = ExecStatus::FAILED;
            r.message = "node structural validation failed: " + templ;
            return r;
        }

        std::string name = item.contains("name") && !item["name"].get<std::string>().empty()
            ? item["name"].get<std::string>() : node->getName();
        trace(ctx, "> node: " + name + " [" + templ + "]");

        // 参数：条目级优先，为空则用节点默认参数
        nlohmann::json params;
        if (item.contains("params") && !item["params"].is_null() &&
            (!item["params"].is_array() || !item["params"].empty())) {
            params = resolveValue(item["params"], ctx);
        }
        else {
            params = resolveValue(node->getParams(), ctx);
        }
        // 将参数暴露为 ${param_N} 供分支消息使用
        if (params.is_array()) {
            for (size_t i = 0; i < params.size(); ++i) {
                ctx.vars.set("param_" + std::to_string(i), params[i]);
            }
        }
        // 将条目参数按节点默认参数中的占位符名绑定（如 ${zero_threshold}、${max_wait}）
        if (params.is_array()) {
            const auto& defaults = node->getParams();   // std::vector<nlohmann::json>
            for (size_t i = 0; i < params.size() && i < defaults.size(); ++i) {
                if (defaults[i].is_string()) {
                    std::string d = defaults[i].get<std::string>();
                    if (d.size() > 3 && d[0] == '$' && d[1] == '{' && d.back() == '}') {
                        ctx.vars.set(d.substr(2, d.size() - 3), params[i]);
                    }
                }
            }
        }

        const L1Action* act = nullptr;
        const std::string& act_tmpl = node->getAction().template_path;
        if (node->isActionInline()) {
            // 内联动作：仅记录，仿真按通用动作处理
            act = nullptr;
        }
        else {
            for (const auto& key : refKeyCandidates(act_tmpl)) {
                act = actions_.getAction(key);
                if (act) break;
            }
            if (!act) {
                r.status = ExecStatus::REFERENCE_ERROR;
                r.message = "L1 action template not found: " + act_tmpl;
                return r;
            }
        }

        nlohmann::json out;
        bool timed_out = false;
        uint32_t attempts = static_cast<uint32_t>(node->getMaxRetries()) + 1;
        for (uint32_t a = 0; a < attempts; ++a) {
            if (!checkSteps(ctx, r)) return r;
            if (act) {
                try {
                    r = execAction(*act, params, ctx, out, timed_out);
                }
                catch (const std::exception& e) {
                    r.status = ExecStatus::INTERNAL_ERROR;
                    r.message = std::string("action execution exception [") + act->getType() + "] " + e.what()
                        + " (params=" + params.dump() + ")";
                    return r;
                }
                if (r.status != ExecStatus::SUCCESS) return r;
            }
            else {
                out = true;
                trace(ctx, "  (inline action, simulated success)");
            }
            if (timed_out) break;
            if (!out.is_null()) break;
        }

        // Judge 求值
        bool success = true;
        if (node->hasJudge()) {
            const JudgeConfig& j = node->getJudge().value();
            switch (j.type) {
            case JudgeType::EXISTS: success = !out.is_null(); break;
            case JudgeType::COMPARE: {
                // judge 的 value 字段存储为字符串：解析为数值或字符串后再比较
                std::string rhs_str = resolveStr(j.value, ctx);
                nlohmann::json rhs;
                try {
                    size_t p = 0;
                    long long iv = std::stoll(rhs_str, &p);
                    if (p == rhs_str.size()) rhs = nlohmann::json(iv);
                    else rhs = nlohmann::json(std::stod(rhs_str));
                }
                catch (...) { rhs = nlohmann::json(rhs_str); }
                if (!compareValues(out, rhs, j.condition, success)) {
                    r.status = ExecStatus::CONDITION_ERROR;
                    r.message = "judge comparison unsupported: " + j.condition;
                    return r;
                }
                break;
            }
            case JudgeType::EXPRESSION: {
                std::string err;
                success = evalExpression(resolveStr(j.expression, ctx), ctx.vars, err);
                if (!err.empty()) {
                    r.status = ExecStatus::CONDITION_ERROR;
                    r.message = "judge expression error: " + err;
                    return r;
                }
                break;
            }
            default: success = true; break;
            }
        }

        // 保存动作层结果（动作签名中的 result_key）
        if (act && !out.is_null()) {
            const std::string& act_key = act->getSignature().result_key;
            if (!act_key.empty()) ctx.vars.set(act_key, out);
        }
        // 保存节点结果：bool 返回类型 → judge 判定结果；其他类型 → 动作原始输出
        // 节点签名 result_key 与条目级 result_key 可并存（如 final_pressure 与 p_end 同时落盘）
        std::vector<std::string> result_keys;
        if (!node->getSignature().result_key.empty()) result_keys.push_back(node->getSignature().result_key);
        if (item.contains("result_key") && item["result_key"].is_string()) {
            std::string ik = item["result_key"].get<std::string>();
            if (std::find(result_keys.begin(), result_keys.end(), ik) == result_keys.end()) {
                result_keys.push_back(ik);
            }
        }
        for (const auto& result_key : result_keys) {
            nlohmann::json rv = out;
            if (node->getSignature().return_type == DataType::B) rv = nlohmann::json(success);
            ctx.vars.set(result_key, rv);
            trace(ctx, "  -> " + result_key + " = " + jsonToStr(rv));
        }
        // 仿真语义约定：set_test_result 节点写 test_result 变量
        if (node->getFilename().find("set_test_result") != std::string::npos &&
            params.is_array() && !params.empty()) {
            ctx.vars.set("test_result", params[0]);
        }

        if (timed_out) {
            trace(ctx, "  timeout (no response), running on_timeout");
            ctx.audit.log(ctx.time_ms, "WARN", "node timeout: " + name);
            r = execBranchItems(node->getOnTimeout(), ctx);
        }
        else if (success) {
            r = execBranchItems(node->getOnSuccess(), ctx);
        }
        else {
            ctx.audit.log(ctx.time_ms, "ERROR", "node failed: " + name);
            r = execBranchItems(node->getOnFailure(), ctx);
        }
        return r;
    }

    ExecResult Executor::execAction(const L1Action& act, const nlohmann::json& args,
        ExecContext& ctx, nlohmann::json& out, bool& timed_out) {
        ExecResult r;
        std::string type = act.getType();

        // 参数名 → 值（含默认值）；纯数字字符串归一为数值（${变量} 解析产物）
        std::map<std::string, nlohmann::json> amap;
        for (size_t i = 0; i < act.getArgs().size(); ++i) {
            const ArgDef& d = act.getArgs()[i];
            nlohmann::json v;
            if (args.is_array() && i < args.size()) v = args[i];
            else if (d.default_value.has_value()) v = nlohmann::json(*d.default_value);
            else v = nlohmann::json();
            if (v.is_string()) {
                const std::string& sv = v.get<std::string>();
                bool all_digits = !sv.empty() &&
                    std::all_of(sv.begin(), sv.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
                if (all_digits) {
                    try { v = nlohmann::json(std::stoll(sv)); } catch (...) {}
                }
            }
            amap[d.name] = v;
        }

        // 占位符替换（支持 ${name_high} / ${name_low} 字节拆分）
        auto substitute = [&](const std::string& s) -> std::string {
            std::string out_s;
            size_t pos = 0;
            while (pos < s.size()) {
                size_t b = s.find("${", pos);
                if (b == std::string::npos) { out_s += s.substr(pos); break; }
                out_s += s.substr(pos, b - pos);
                size_t e = s.find('}', b);
                if (e == std::string::npos) { out_s += s.substr(b); break; }
                std::string name = s.substr(b + 2, e - b - 2);
                std::string base = name;
                int part = -1; // -1 整体 0 高字节 1 低字节
                if (name.size() > 5 && name.substr(name.size() - 5) == "_high") {
                    base = name.substr(0, name.size() - 5); part = 0;
                }
                else if (name.size() > 4 && name.substr(name.size() - 4) == "_low") {
                    base = name.substr(0, name.size() - 4); part = 1;
                }
                auto it = amap.find(base);
                if (it != amap.end()) {
                    if (part < 0) {
                        // 按参数类型格式化
                        std::string tn;
                        for (const auto& d : act.getArgs()) if (d.name == base) tn = d.getTypeName();
                        out_s += formatArg(it->second, tn);
                    }
                    else {
                        int64_t n = it->second.is_number_integer()
                            ? it->second.get<int64_t>() : static_cast<int64_t>(it->second.get<double>());
                        int byte = part == 0 ? (int)((n >> 8) & 0xFF) : (int)(n & 0xFF);
                        out_s += (char)("0123456789ABCDEF"[byte >> 4]);
                        out_s += (char)("0123456789ABCDEF"[byte & 0x0F]);
                    }
                }
                else {
                    out_s += s.substr(b, e - b + 1);
                }
                pos = e + 1;
            }
            return out_s;
        };

        std::string req = substitute(act.getRequest());

        // 设备：优先 slave_id 参数，否则 default
        std::string device = "default";
        auto sid = amap.find("slave_id");
        if (sid != amap.end() && !sid->second.is_null()) {
            device = jsonToStr(sid->second);
        }

        auto advanceComm = [&](uint64_t t) { advanceTime(ctx, t); };

        if (type == "modbus_write_verify") {
            if (!ctx.devices.isOnline(device)) {
                advanceComm(act.getTimeoutMs());
                timed_out = true;
                return r;
            }
            std::istringstream ss(req);
            std::vector<std::string> tok;
            std::string t;
            while (ss >> t) tok.push_back(t);
            bool ok = true;
            int64_t fn = tok.empty() ? -1 : parseHexToken(tok[0], ok);
            if (tok.size() >= 5 && fn == 5) {
                int64_t addr = (parseHexToken(tok[1], ok) << 8) | parseHexToken(tok[2], ok);
                bool on = parseHexToken(tok[3], ok) != 0;
                ctx.devices.setCoil(device, static_cast<uint16_t>(addr), on);
                out = true;
                trace(ctx, "  <=> write coil dev=" + device + " addr=0x" + formatArgHex(nlohmann::json(addr), 0) +
                    " val=" + (on ? "ON" : "OFF"));
            }
            else if (tok.size() >= 5 && fn == 6) {
                int64_t addr = (parseHexToken(tok[1], ok) << 8) | parseHexToken(tok[2], ok);
                int64_t val = (parseHexToken(tok[3], ok) << 8) | parseHexToken(tok[4], ok);
                ctx.devices.setRegister(device, static_cast<uint16_t>(addr), val);
                out = true;
                trace(ctx, "  <=> write register dev=" + device + " addr=0x" + formatArgHex(nlohmann::json(addr), 0) +
                    " value=" + std::to_string(val));
                ctx.audit.log(ctx.time_ms, "CHANGE", "register write", {
                    {"device", device}, {"register", addr}, {"value", val}, {"request", req} });
            }
            else {
                out = true;
                trace(ctx, "  <-> write (simulated): " + req);
            }
            advanceComm(5);
            return r;
        }

        if (type == "modbus_read_cache" || type == "modbus_read_check") {
            if (!ctx.devices.isOnline(device)) {
                advanceComm(act.getTimeoutMs());
                timed_out = true;
                return r;
            }
            std::istringstream ss(req);
            std::vector<std::string> tok;
            std::string t;
            while (ss >> t) tok.push_back(t);
            bool ok = true;
            int64_t fn = tok.empty() ? -1 : parseHexToken(tok[0], ok);
            int64_t addr = 0;
            if (fn == 3 && tok.size() >= 6) {
                // 含从站字节: 03 slave addr_hi addr_lo cnt_hi cnt_lo
                addr = (parseHexToken(tok[2], ok) << 8) | parseHexToken(tok[3], ok);
            }
            else if (fn == 3 && tok.size() >= 5) {
                // 无从站字节，双字节地址: 03 addr_hi addr_lo cnt_hi cnt_lo
                addr = (parseHexToken(tok[1], ok) << 8) | parseHexToken(tok[2], ok);
            }
            else if (fn == 3 && tok.size() >= 4) {
                // 无从站字节，单字节地址: 03 addr cnt_hi cnt_lo
                addr = parseHexToken(tok[1], ok);
            }
            out = nlohmann::json(ctx.devices.getRegister(device, static_cast<uint16_t>(addr)));
            const ParseConfig* pc = act.getParse();
            if (pc && pc->type == "uint16" && out.is_number_integer()) {
                out = nlohmann::json(static_cast<int64_t>(out.get<int64_t>() & 0xFFFF));
            }
            trace(ctx, "  <=> read register dev=" + device + " addr=0x" + formatArgHex(nlohmann::json(addr), 0) +
                " → " + jsonToStr(out));
            if (type == "modbus_read_check" && act.getCheck() != nullptr) {
                nlohmann::json cv = resolveValue(nlohmann::json(act.getCheck()->value), ctx);
                bool cok = false;
                compareValues(out, cv, act.getCheck()->condition, cok);
                out = nlohmann::json(cok);
            }
            advanceComm(5);
            return r;
        }

        if (type == "wait" || type == "wait_ms" || type == "wait_seconds" || type == "delay") {
            uint64_t dt = args.is_array() && !args.empty() && args[0].is_number()
                ? static_cast<uint64_t>(args[0].get<int64_t>()) : 100;
            if (type == "wait_seconds") {
                dt *= 1000;
            }
            else if (args.is_array() && args.size() > 1 && args[1].is_string() &&
                args[1].get<std::string>() == "s") {
                dt *= 1000;   // 显式参数单位 s
            }
            else if (act.getUnit() == "s" || act.getUnit() == "sec") {
                dt *= 1000;   // 动作级显式单位 s（默认按 ms 处理）
            }
            advanceTime(ctx, dt);
            out = true;
            trace(ctx, "  waiting " + std::to_string(dt) + "ms");
            return r;
        }

        if (type == "log") {
            std::string msg = args.is_array() && !args.empty() ? jsonToStr(args[0]) : act.getDescription();
            trace(ctx, "  * log: " + resolveStr(msg, ctx));
            ctx.audit.log(ctx.time_ms, "INFO", resolveStr(msg, ctx));
            out = true;
            return r;
        }

        if (type == "popup") {
            std::string msg = args.is_array() && !args.empty() ? jsonToStr(args[0]) : act.getDescription();
            std::string level = args.is_array() && args.size() > 1 ? jsonToStr(args[1]) : "info";
            trace(ctx, "  * popup[" + level + "]: " + resolveStr(msg, ctx));
            ctx.audit.log(ctx.time_ms, "ERROR", "HMI popup: " + resolveStr(msg, ctx));
            out = true;
            return r;
        }

        if (type == "set_variable" || type == "set") {
            if (args.is_array() && args.size() >= 2) {
                ctx.vars.set(jsonToStr(args[0]), resolveValue(args[1], ctx));
                trace(ctx, "  => variable " + jsonToStr(args[0]) + " = " + jsonToStr(ctx.vars.get(jsonToStr(args[0])) ? *ctx.vars.get(jsonToStr(args[0])) : nlohmann::json()));
            }
            else if (args.is_array() && args.size() == 1) {
                ctx.vars.set("result", resolveValue(args[0], ctx));
                trace(ctx, "  => variable result = " + jsonToStr(args[0]));
            }
            out = true;
            ctx.audit.log(ctx.time_ms, "CHANGE", "variable set", { {"args", args} });
            return r;
        }

        if (type == "read_variable") {
            const nlohmann::json* v = args.is_array() && !args.empty()
                ? ctx.vars.get(jsonToStr(args[0])) : nullptr;
            out = v ? *v : nlohmann::json(false);
            trace(ctx, "  <= read variable -> " + jsonToStr(out));
            return r;
        }

        if (type == "calculate" || type == "calculate_check") {
            if (act.hasExpression()) {
                // 表达式分派：含比较运算符 → 布尔条件；否则 → 四则运算
                std::string es = resolveStr(*act.getExpression(), ctx);
                bool is_compare = es.find("==") != std::string::npos || es.find("!=") != std::string::npos ||
                    es.find(">=") != std::string::npos || es.find("<=") != std::string::npos ||
                    es.find('>') != std::string::npos || es.find('<') != std::string::npos;
                if (is_compare) {
                    std::string err;
                    bool b = evalExpression(*act.getExpression(), ctx.vars, err);
                    out = nlohmann::json(b);
                    if (!err.empty()) trace(ctx, "  (calc expression: " + err + ")");
                }
                else {
                    double v = 0;
                    if (evalArith(es, v)) out = nlohmann::json(v);
                    else out = nlohmann::json(es);
                }
            }
            else if (args.is_array() && !args.empty()) {
                if (args[0].is_string()) {
                    // 字符串表达式：先替换占位符，再做四则运算求值
                    std::string es = resolveStr(args[0].get<std::string>(), ctx);
                    double v = 0;
                    if (evalArith(es, v)) out = nlohmann::json(v);
                    else out = nlohmann::json(es);
                }
                else {
                    out = args[0];
                }
            }
            else out = true;
            trace(ctx, "  = compute -> " + jsonToStr(out));
            return r;
        }

        // 通用动作（ui_action / user_decision / script_exec / 数字IO / 阀门 / 夹具等）：
        // 仿真中按成功处理并记审计，推进小段时间
        if (type == "user_decision" && ctx.confirm_between) {
            ctx.audit.log(ctx.time_ms, "OPERATOR_CONFIRM", "operator decision confirmed: " + act.getDescription());
        }
        trace(ctx, "  ~ virtual exec " + type + ": " + act.getDescription());
        ctx.audit.log(ctx.time_ms, "INFO", "simulated action " + type, { {"description", act.getDescription()} });
        out = true;
        advanceTime(ctx, 10);
        return r;
    }

    ExecResult Executor::execGroupMode(const nlohmann::json& g, ExecContext& ctx) {
        ExecResult r;
        L3Group vg(g);
        if (!vg.validate()) {
            r.status = ExecStatus::FAILED;
            r.message = "L3 structural validation failed: " + g.value("name", std::string("?"));
            return r;
        }
        std::string mode = g.value("mode", "sequence");
        uint64_t start = ctx.time_ms;
        uint64_t timeout = g.contains("timeout_ms") ? timeoutValue(g["timeout_ms"], ctx) : 0;

        auto checkGroupTimeout = [&]() -> bool {
            return timeout > 0 && (ctx.time_ms - start) > timeout;
        };

        if (mode == "sequence" || mode == "parallel") {
            r = execBody(g.value("body", nlohmann::json()), ctx, mode == "parallel");
            if (checkGroupTimeout() && r.status == ExecStatus::SUCCESS) {
                r.status = ExecStatus::TIMEOUT;
                r.message = "group timeout: " + g.value("name", std::string("?"));
            }
            return r;
        }

        if (mode == "if") {
            std::string err;
            bool cond = evalCondition(g.value("condition", nlohmann::json()), ctx.vars, err);
            if (!err.empty()) {
                r.status = ExecStatus::CONDITION_ERROR;
                r.message = "condition evaluation failed: " + err;
                return r;
            }
            trace(ctx, std::string("> condition branch -> ") + (cond ? "then" : "else"));
            return execBody(cond ? g.value("then", nlohmann::json()) : g.value("else", nlohmann::json()),
                ctx, false);
        }

        if (mode == "switch") {
            std::string err;
            bool matched = false;
            for (const auto& c : g.value("cases", nlohmann::json::array())) {
                std::string cv = resolveStr(c.value("case", std::string("")), ctx);
                std::string src = resolveStr(c.value("value", std::string("")), ctx);
                if (!cv.empty() && cv == src) {
                    matched = true;
                    r = execBody(c.value("body", nlohmann::json::array()), ctx, false);
                    break;
                }
            }
            if (!matched) r = execBody(g.value("default", nlohmann::json::array()), ctx, false);
            return r;
        }

        if (mode == "loop") {
            nlohmann::json loop = g.value("loop", nlohmann::json::object());
            std::string ltype = loop.value("type", "count");
            int64_t count = loop.value("count", 0);
            int64_t max_iter = loop.value("max_iterations", 10000);
            uint64_t loop_timeout = loop.contains("timeout_ms") ? timeoutValue(loop["timeout_ms"], ctx) : timeout;
            int64_t iter = 0;

            // 循环条件按表达式求值（支持 ${变量} 比较与布尔字面量）
            auto evalLoopCond = [&](bool& ok) -> bool {
                std::string err;
                bool c = evalExpression(loop.value("condition", "true"), ctx.vars, err);
                if (!err.empty()) {
                    ok = false;
                    r.status = ExecStatus::CONDITION_ERROR;
                    r.message = "loop condition evaluation failed: " + err;
                    return false;
                }
                ok = true;
                return c;
            };
            auto bodyOnce = [&]() -> ExecResult {
                return execBody(g.value("body", nlohmann::json()), ctx, false);
            };
            auto guard = [&]() -> bool {
                if (iter >= max_iter) {
                    r.status = ExecStatus::STEP_BUDGET_EXCEEDED;
                    r.message = "loop iterations exceeded limit (" + std::to_string(max_iter) + "), possible infinite loop";
                    return false;
                }
                if (loop_timeout > 0 && (ctx.time_ms - start) > loop_timeout) {
                    r.status = ExecStatus::TIMEOUT;
                    r.message = "loop timeout (" + std::to_string(loop_timeout) + "ms): " + g.value("name", std::string("?"));
                    return false;
                }
                return true;
            };

            trace(ctx, "loop " + ltype + " [" +
                (loop.contains("condition") ? loop["condition"].get<std::string>() : std::to_string(count)) + "]");
            ctx.indent++;
            if (ltype == "count") {
                for (iter = 0; iter < count; ++iter) {
                    if (!guard()) break;
                    ExecResult br = bodyOnce();
                    if (br.status != ExecStatus::SUCCESS) { r = br; break; }
                }
            }
            else if (ltype == "while") {
                while (true) {
                    if (!guard()) break;
                    bool ok = true;
                    bool c = evalLoopCond(ok);
                    if (!ok) break;   // 条件求值错误
                    if (!c) break;    // 条件为假，正常退出
                    ExecResult br = bodyOnce();
                    if (br.status != ExecStatus::SUCCESS) { r = br; break; }
                    ++iter;
                }
            }
            else if (ltype == "do-while" || ltype == "dowhile") {
                while (true) {
                    if (!guard()) break;
                    ExecResult br = bodyOnce();
                    if (br.status != ExecStatus::SUCCESS) { r = br; break; }
                    ++iter;
                    bool ok = true;
                    bool c = evalLoopCond(ok);
                    if (!ok) break;   // 条件求值错误
                    if (!c) break;    // 条件为假，正常退出
                }
            }
            else if (ltype == "until") {
                while (true) {
                    if (!guard()) break;
                    ExecResult br = bodyOnce();
                    if (br.status != ExecStatus::SUCCESS) { r = br; break; }
                    ++iter;
                    bool ok = true;
                    bool c = evalLoopCond(ok);
                    if (!ok) break;
                    if (c) break;     // 条件为真，停止
                }
            }
            else if (ltype == "foreach") {
                std::string items_name = loop.value("items", "");
                std::string item_name = loop.value("item_name", "item");
                const nlohmann::json* items = ctx.vars.get(items_name);
                if (!items || !items->is_array()) {
                    r.status = ExecStatus::CONDITION_ERROR;
                    r.message = "foreach list variable undefined or not an array: " + items_name;
                }
                else {
                    for (iter = 0; iter < static_cast<int64_t>(items->size()); ++iter) {
                        if (!guard()) break;
                        ctx.vars.set(item_name, (*items)[static_cast<size_t>(iter)]);
                        ExecResult br = bodyOnce();
                        if (br.status != ExecStatus::SUCCESS) { r = br; break; }
                    }
                }
            }
            else {
                r.status = ExecStatus::INTERNAL_ERROR;
                r.message = "unknown loop type: " + ltype;
            }
            ctx.indent--;
            if (r.status == ExecStatus::TIMEOUT || r.status == ExecStatus::STEP_BUDGET_EXCEEDED) {
                trace(ctx, "  " + r.message + ", running on_timeout");
                ctx.audit.log(ctx.time_ms, "WARN", r.message);
                ExecResult br = execBranchItems(g.value("on_timeout", nlohmann::json()), ctx);
                if (br.status != ExecStatus::SUCCESS) r = br;
            }
            return r;
        }

        r.status = ExecStatus::INTERNAL_ERROR;
        r.message = "unknown group mode: " + mode;
        return r;
    }

    ExecResult Executor::runGroupJson(const nlohmann::json& group_json,
        const nlohmann::json& call_params, ExecContext& ctx) {
        ExecResult r;
        L3Group vg(group_json);
        if (!vg.validate()) {
            r.status = ExecStatus::FAILED;
            r.message = "L3 structural validation failed: " + group_json.value("name", std::string("?"));
            return r;
        }
        // 绑定调用参数到参数名
        for (const auto& d : vg.getArgs()) {
            nlohmann::json v;
            if (call_params.is_array() && d.index >= 0 && d.index < static_cast<int>(call_params.size())) {
                v = call_params[d.index];
            }
            else if (call_params.is_object() && call_params.contains(d.name)) {
                v = call_params[d.name];
            }
            else if (d.default_value.has_value()) {
                v = nlohmann::json(*d.default_value);
            }
            if (!v.is_null()) ctx.vars.set(d.name, resolveValue(v, ctx));
        }
        r = execGroupMode(group_json, ctx);
        return r;
    }

    ExecResult Executor::runFlowJson(const std::string& name, const nlohmann::json& flow_json,
        ExecContext& ctx) {
        ExecResult r;
        auto t0 = std::chrono::steady_clock::now();
        L4Flow f(flow_json);
        if (flow_json.is_null() || !flow_json.contains("body") || !flow_json["body"].is_array() ||
            flow_json["body"].empty()) {
            r.status = ExecStatus::FAILED;
            r.message = "L4 structural validation failed (missing body): " + name;
            return r;
        }
        if (!f.validate()) {
            // 结构校验未通过：记审计警告后继续仿真执行，以暴露更深层问题
            ctx.audit.log(ctx.time_ms, "WARN", "L4 structural validation not passed (continuing simulation): " + name);
        }
        trace(ctx, "▶▶ Flow: " + name + (f.getVersion().empty() ? "" : " v" + f.getVersion()));
        // 应用当前 profile 参数
        for (const auto& kv : f.getCurrentProfileParams()) {
            ctx.vars.set(kv.first, nlohmann::json(kv.second));
            ctx.vars.set("profile." + kv.first, nlohmann::json(kv.second));
        }
        uint64_t start = ctx.time_ms;
        ctx.indent++;
        r = execBody(f.getBody(), ctx, false);
        ctx.indent--;
        if (f.hasTimeout() && ctx.time_ms - start > f.getTimeoutMs() && r.status == ExecStatus::SUCCESS) {
            r.status = ExecStatus::TIMEOUT;
            r.message = "flow timeout: " + name;
        }
        if ((r.status == ExecStatus::TIMEOUT) && f.hasOnTimeout()) {
            trace(ctx, "flow timeout, running on_timeout");
            r = execBranchItems(f.getOnTimeout(), ctx);
        }
        r.steps = ctx.steps;
        r.sim_time_ms = ctx.time_ms;
        r.elapsed_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count());
        return r;
    }

    ExecResult Executor::runGroupFile(const std::string& group_file,
        const nlohmann::json& call_params, ExecContext& ctx) {
        ExecResult r;
        auto t0 = std::chrono::steady_clock::now();
        try {
            std::ifstream in(group_file);
            nlohmann::json j;
            in >> j;
            r = runGroupJson(j, call_params, ctx);
        }
        catch (const std::exception& e) {
            r.status = ExecStatus::INTERNAL_ERROR;
            r.message = std::string("parse failed: ") + e.what();
        }
        r.steps = ctx.steps;
        r.sim_time_ms = ctx.time_ms;
        r.elapsed_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count());
        return r;
    }

    ExecResult Executor::runFlowFile(const std::string& flow_file, ExecContext& ctx) {
        ExecResult r;
        auto t0 = std::chrono::steady_clock::now();
        try {
            std::ifstream in(flow_file);
            nlohmann::json j;
            in >> j;
            r = runFlowJson(j.value("name", flow_file), j, ctx);
        }
        catch (const std::exception& e) {
            r.status = ExecStatus::INTERNAL_ERROR;
            r.message = std::string("parse failed: ") + e.what();
        }
        r.steps = ctx.steps;
        r.sim_time_ms = ctx.time_ms;
        r.elapsed_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count());
        return r;
    }

} // namespace industrial_config_engine
