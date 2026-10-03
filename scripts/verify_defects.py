"""Reproduce configuration defects and repairs using the real C++ engine.

python scripts/verify_defects.py
Requires Python 3.10+ and an existing C++17 toolchain. No physical device I/O.
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]
EXPECTATIONS = ROOT / "scripts/defect_expectations.json"


class VerificationError(RuntimeError):
    pass


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def write(path, data):
    Path(path).write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def digest(data):
    return hashlib.sha256(json.dumps(data, sort_keys=True, ensure_ascii=False,
                                    separators=(",", ":")).encode("utf-8")).hexdigest()


def validate_output(path):
    path = Path(path).resolve()
    protected = [ROOT / name for name in
                 ("examples", "scripts", "src", "include", "tests", "docs", "third_party", ".git")]
    if path == ROOT or path in ROOT.parents or any(
            path == item or item in path.parents or path in item.parents for item in protected):
        raise VerificationError("Output overlaps repository sources: " + str(path))
    return path


def run(command, log, *, timeout=240, env=None):
    with log.open("a", encoding="utf-8") as stream:
        stream.write("$ " + subprocess.list2cmdline([str(x) for x in command]) + "\n")
        stream.flush()
        try:
            process = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT,
                                     timeout=timeout, env=env)
        except subprocess.TimeoutExpired as error:
            raise VerificationError(f"Command timed out; see {log}") from error
    if process.returncode:
        raise VerificationError(f"Command failed ({process.returncode}); see {log}")


def build_probe(output, compiler=None):
    build = output / "cpp"
    build.mkdir(parents=True, exist_ok=True)
    log = output / "build.log"
    log.write_text("", encoding="utf-8")
    executable = build / ("defect_probe.exe" if os.name == "nt" else "defect_probe")
    compiler_path = compiler or os.environ.get("ICE_CXX")
    # Optional existing workspace compiler; never downloads or installs anything.
    portable_zig = ROOT.parent / "beverage-cpp-toolchain/zig-windows-x86_64-0.13.0/zig.exe"
    if not compiler_path and portable_zig.is_file():
        compiler_path = str(portable_zig)
    if not compiler_path:
        compiler_path = next((shutil.which(name) for name in ("zig", "clang++", "g++")
                              if shutil.which(name)), None)
    cmake = shutil.which("cmake") if not compiler_path else None
    if not compiler_path and not cmake:
        raise VerificationError("No C++17 compiler found. Set ICE_CXX, use --compiler with g++/clang++/zig, "
                                "or configure CMake with a C++ toolchain. No checks were skipped.")
    sources = sorted((ROOT / "src").glob("*.cpp")) + [ROOT / "tests/defect_probe.cpp"]
    inputs = sources + sorted((ROOT / "include").rglob("*.hpp")) + sorted((ROOT / "third_party").rglob("*.hpp"))
    inputs += [ROOT / "CMakeLists.txt", Path(__file__)]
    identity = {"tool": str(compiler_path or cmake), "sources": {
        str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}}
    tool_path = Path(compiler_path or cmake)
    if tool_path.is_file():
        identity["tool_stat"] = [tool_path.stat().st_size, tool_path.stat().st_mtime_ns]
    key = digest(identity)
    stamp = build / "build.json"
    if stamp.exists():
        previous = read(stamp)
        cached = Path(previous.get("executable", ""))
        if previous.get("source_digest") == key and cached.is_file() and previous.get("binary_sha256") == hashlib.sha256(cached.read_bytes()).hexdigest():
            return cached, {**previous, "cached_build": True}
    print("Building real C++ parser/executor probe...", flush=True)
    if compiler_path:
        tool = str(compiler_path)
        command = [tool]
        env = os.environ.copy()
        if Path(tool).stem.lower() == "zig":
            command += ["c++"]
            env["ZIG_GLOBAL_CACHE_DIR"] = str(build / "cache-global")
            env["ZIG_LOCAL_CACHE_DIR"] = str(build / "cache-local")
        command += ["-std=c++17", "-O0", "-I", str(ROOT / "include"), "-I", str(ROOT / "third_party")]
        command += [str(path) for path in sources] + ["-o", str(executable)]
        run(command, log, timeout=900, env=env)
    else:
        run([cmake, "-S", str(ROOT), "-B", str(build), "-DBUILD_DEMOS=OFF", "-DBUILD_TESTING=ON"], log)
        run([cmake, "--build", str(build), "--config", "Release", "--target", "defect_probe"], log, timeout=900)
        if not executable.exists():
            executable = build / "Release" / executable.name
    if not executable.is_file():
        raise VerificationError("Build did not produce defect_probe; see " + str(log))
    evidence = {"source_digest": key, "executable": str(executable),
                "binary_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
                "compiler": str(compiler_path or cmake), "cached_build": False}
    write(stamp, evidence)
    return executable, evidence


def observation(result):
    """Strict deterministic regression oracle, separate from engine validation."""
    simulation = result["simulation"]
    return {"events": [entry for entry in simulation["audit"] if entry["level"] in ("ROUTE", "CHANGE")],
            "variables": simulation["variables"], "devices": simulation.get("devices"),
            "sim_time_ms": simulation["sim_time_ms"]}


def first_difference(left, right, path="observation"):
    if type(left) is not type(right):
        return f"{path}: {type(left).__name__} != {type(right).__name__}"
    if isinstance(left, dict):
        if left.keys() != right.keys():
            return f"{path}: keys differ ({sorted(set(left) ^ set(right))})"
        for key in left:
            difference = first_difference(left[key], right[key], f"{path}.{key}")
            if difference: return difference
    elif isinstance(left, list):
        if len(left) != len(right): return f"{path}: length {len(left)} != {len(right)}"
        for index, (a, b) in enumerate(zip(left, right)):
            difference = first_difference(a, b, f"{path}[{index}]")
            if difference: return difference
    elif left != right:
        return f"{path}: {str(left)[:120]} != {str(right)[:120]}"
    return None


def summary(result, baseline):
    simulation = result["simulation"]
    difference = first_difference(observation(baseline), observation(result))
    return {"structural_accepted": result["structural"]["accepted"],
            "structural_errors": result["structural"]["errors"],
            "simulation_status": simulation["status"], "simulation_message": simulation["message"],
            "warnings": [e["message"] for e in simulation["audit"] if e["level"] in ("WARN", "ERROR")],
            "oracle_matched": difference is None, "oracle_difference": difference,
            "observation_sha256": digest(observation(result)),
            "steps": simulation["steps"], "sim_time_ms": simulation["sim_time_ms"]}


def probe_path(path):
    try:
        return os.path.relpath(path, ROOT)
    except ValueError:  # Different Windows drives.
        return str(path)


def measured_signature(case):
    result = {key: case["faulty"][key] for key in
              ("structural_accepted", "simulation_status", "oracle_matched")}
    result["control_observation_sha256"] = case["control"]["observation_sha256"]
    return result


def table_cell(value):
    return str(value).replace("|", "\\|").replace("\n", " ")


def write_markdown(output, report):
    fence = chr(96) * 3
    lines = ["# 配置缺陷与修正复现实验", "", f"本次结果：**{report['status']}**。", "",
             "PASS 表示对照、修正和已登记的缺陷表现得到重现，不表示引擎发现了所有缺陷。",
             "结构列调用现有 C++ 顶层解析/校验 API；模拟列单独调用真实 Executor（内部仍含结构校验），不是关闭结构校验后的消融。结果对照是测试程序的严格回归检查，不是第三个引擎闸门或人工审核。", "",
             "| 案例 | 样例 | 缺陷 | 结构 | 模拟 | 结果对照 | 修正 |",
             "|---|---|---|---|---|---|---|"]
    for case in report["cases"]:
        faulty = case["faulty"]
        values = [f"[{case['id']}](cases/{case['id']}/case.md)", case["example"], case["description"],
                  "接受" if faulty["structural_accepted"] else "拒绝", faulty["simulation_status"],
                  "未发现差异" if faulty["oracle_matched"] else "发现差异", "通过" if case["repair_passed"] else "失败"]
        lines += ["| " + " | ".join(map(table_cell, values)) + " |"]
    lines += ["", "## 本次统计", "", fence + "json", json.dumps(report.get("metrics", {}), ensure_ascii=False, indent=2), fence, "",
              "## 实验边界", ""] + ["- " + item for item in report["limitations"]]
    if report.get("errors"): lines += ["", "## 未通过项", ""] + ["- " + error for error in report["errors"]]
    (output / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def verify(output, compiler=None, *, check_expected=True):
    from defect_cases import build_cases
    output = validate_output(output)
    output.mkdir(parents=True, exist_ok=True)
    report = {"status": "RUNNING", "started_utc": datetime.now(timezone.utc).isoformat(), "cases": [], "errors": [],
              "limitations": ["人工构造的有限配置变异集，不是随机 LLM 生成错误分布；检测比例只适用于本组案例。",
                              "各样例使用确定性虚拟设备；部分案例验证局部 L3 子过程，不是每次都运行完整生产线。",
                              "顶层结构检查与 Executor 分别调用；Executor 内部仍执行结构检查，不能当作关闭结构校验后的消融。嵌套模板检查可能推迟到执行时。",
                              "结果对照比较 ROUTE/CHANGE 事件及其虚拟时间、最终变量、设备状态；要求严格一致，不能证明任意行为等价。",
                              "仅告警但继续执行不计为模拟拒绝；异常崩溃、探针失败与探针进程的墙钟超时不计为有效缺陷检测。",
                              "饮料 C++ 模型立即完成设备动作，寄存器为工程值；不测真实通信、浮点字节解码或 UI 渲染。",
                              "没有独立人工审核、审核一致率或真实硬件测试；修正是明确恢复基准配置，不是自动推导修复。"]}
    write(output / "report.json", report)
    started = time.monotonic()
    try:
        probe, report["build"] = build_probe(output, compiler)
        cases = build_cases(ROOT)
        if not cases or len({c["id"] for c in cases}) != len(cases):
            raise VerificationError("Defect cases are empty or identifiers are duplicated")
        for index, case in enumerate(cases, 1):
            print(f"[{index}/{len(cases)}] {case['id']}: control -> defect -> repair", flush=True)
            destination = output / "cases" / case["id"]
            destination.mkdir(parents=True, exist_ok=True)
            results = {}
            for variant, request_key in (("control", "control_request"), ("faulty", "faulty_request"), ("fixed", "fixed_request")):
                write(destination / (variant + ".request.json"), case[request_key])
                variant_log = destination / (variant + ".log")
                variant_log.write_text("", encoding="utf-8")
                request_path = destination / (variant + ".request.json")
                result_path = destination / (variant + ".result.json")
                probe_env = os.environ.copy()
                probe_env["ICE_DEFECT_REQUEST"] = str(request_path)
                probe_env["ICE_DEFECT_RESULT"] = str(result_path)
                run([str(probe), probe_path(request_path), probe_path(result_path)],
                    variant_log, timeout=60, env=probe_env)
                results[variant] = read(destination / (variant + ".result.json"))
                result = results[variant]
                if result.get("request_error") or result["simulation"]["status"] in ("NOT_RUN", "INTERNAL_ERROR") or result["simulation"].get("exception"):
                    raise VerificationError(f"{case['id']} {variant}: probe/setup/engine exception, not a successful detection: {result.get('request_error') or result['simulation']['message']}")
            row = {key: value for key, value in case.items() if not key.endswith("_request")}
            for variant in results: row[variant] = summary(results[variant], results["control"])
            row["control_passed"] = row["control"]["structural_accepted"] and row["control"]["simulation_status"] == "SUCCESS"
            row["repair_passed"] = row["fixed"]["structural_accepted"] and row["fixed"]["simulation_status"] == "SUCCESS" and row["fixed"]["oracle_matched"]
            for name in ("control", "repair"):
                if not row[name + "_passed"]: report["errors"].append(case["id"] + ": " + name + " failed")
            old = json.dumps(case["faulty_request"], ensure_ascii=False, indent=2).splitlines(keepends=True)
            new = json.dumps(case["fixed_request"], ensure_ascii=False, indent=2).splitlines(keepends=True)
            patch = "".join(difflib.unified_diff(old, new, "faulty.request.json", "fixed.request.json"))
            if not patch: raise VerificationError(case["id"] + ": defect and repair are identical")
            (destination / "repair.diff").write_text(patch, encoding="utf-8")
            fence = chr(96) * 3
            detail = f"# {case['id']}\n\n{case['description']}\n\n修正：{case['repair']}\n\n"
            detail += "实际判定与诊断：\n\n" + fence + "json\n" + json.dumps(row, ensure_ascii=False, indent=2) + "\n" + fence + "\n\n"
            detail += "修正前后差异（完整请求及执行日志均在本目录）：\n\n" + fence + "diff\n" + patch + "\n" + fence + "\n"
            (destination / "case.md").write_text(detail, encoding="utf-8")
            report["cases"].append(row)
        signatures = {case["id"]: measured_signature(case) for case in report["cases"]}
        write(output / "measured_signatures.json", signatures)
        if check_expected:
            expected = read(EXPECTATIONS)["cases"]
            difference = first_difference(expected, signatures, "expected_cases")
            if difference: report["errors"].append("Registered outcomes changed: " + difference)
        faults = [case["faulty"] for case in report["cases"]]
        controls = [case["control"] for case in report["cases"]]
        structural = sum(not item["structural_accepted"] for item in faults)
        simulation = sum(item["simulation_status"] != "SUCCESS" for item in faults)
        combined = sum(not item["structural_accepted"] or item["simulation_status"] != "SUCCESS" for item in faults)
        report["metrics"] = {"defect_cases": len(faults), "normal_control_runs": len(controls),
            "unique_normal_controls": len({digest(c["control_request"]) for c in cases}),
            "repaired_runs": sum(case["repair_passed"] for case in report["cases"]),
            "top_level_structure_rejections": structural, "executor_path_rejections": simulation,
            "structure_or_executor_rejections": combined, "executor_added_rejections": combined - structural,
            "not_rejected_by_engine": len(faults) - combined,
            "oracle_only_differences": sum(item["structural_accepted"] and item["simulation_status"] == "SUCCESS" and not item["oracle_matched"] for item in faults),
            "undetected_by_all_automatic_checks": sum(item["structural_accepted"] and item["simulation_status"] == "SUCCESS" and item["oracle_matched"] for item in faults),
            "normal_controls_rejected_structure": sum(not item["structural_accepted"] for item in controls),
            "normal_controls_rejected_executor": sum(item["simulation_status"] != "SUCCESS" for item in controls),
            "human_review": "NOT MEASURED"}
        report["status"] = "FAIL" if report["errors"] else "PASS"
    except Exception as error:
        report["status"] = "FAIL"
        report["errors"].append(str(error))
        raise
    finally:
        report["elapsed_seconds"] = round(time.monotonic() - started, 3)
        write(output / "report.json", report)
        write_markdown(output, report)
    if report["errors"]: raise VerificationError("; ".join(report["errors"]))
    print(json.dumps(report["metrics"], ensure_ascii=False, indent=2), flush=True)
    print("PASS: experiments reproduced (including explicitly recorded detection gaps).\nFresh evidence: " + str(output), flush=True)
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/defect-verification")
    parser.add_argument("--compiler", help="existing g++, clang++, or zig executable; alternatively set ICE_CXX")
    args = parser.parse_args(argv)
    try:
        verify(args.output, args.compiler)
    except (VerificationError, OSError, ValueError, KeyError) as error:
        print("FAIL: " + str(error), file=sys.stderr, flush=True)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
