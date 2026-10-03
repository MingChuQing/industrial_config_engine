"""Prepare blinded human-review materials and score two completed human forms.

Preparation never supplies reviewer decisions. Reference labels belong only to
the coordinator; they are experiment expectations, not measured human consensus.
Only Python's standard library is required. No configuration is executed.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
from datetime import datetime
import hashlib
import json
import math
from pathlib import Path
import random
import statistics
import sys
from typing import Any

from defect_cases import build_cases


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SEED = 20261003
SCHEMA_VERSION = 1


class ReviewError(ValueError):
    """Invalid materials, incomplete review, or unsafe output destination."""


def read(path: str | Path) -> Any:
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def digest(value: Any) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=False,
                                     separators=(",", ":")).encode("utf-8")).hexdigest()


def encoded(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, indent=2) + "\n"


def validate_output(path: str | Path, root: str | Path = ROOT) -> Path:
    path, root = Path(path).resolve(), Path(root).resolve()
    protected = [root / name for name in
                 ("examples", "scripts", "src", "include", "tests", "docs",
                  "third_party", ".git", ".codex", ".agents")]
    if path == root or path in root.parents or any(
            path == item or item in path.parents or path in item.parents for item in protected):
        raise ReviewError("Output overlaps protected repository sources: " + str(path))
    return path


# These describe the intended processes, not the individual injected edits.
# Every candidate from the same source receives exactly the same requirement.
REQUIREMENTS: dict[str, dict[str, Any]] = {
    "vacuum/pump_down.group.json": {
        "title": "抽真空并在目标压力停止",
        "requirements": [
            "按顺序打开 V1、关闭 V2、启动真空泵，然后至少读取一次压力。",
            "将读数保存为 pressure；每轮等候 500 ms，仅在压力仍高于 5 Pa 时继续轮询。",
            "达到目标后停止泵；压力循环有 300000 ms 超时，整体有 360000 ms 超时及原有超时停泵处理。",
            "所有节点和动作必须在提供的定义中解析，配置项类型及变量名称应符合语言规则。",
        ],
    },
    "common/system_check.group.json": {
        "title": "系统压力读取检查",
        "requirements": [
            "调用已定义的 read_pressure 节点读取压力，并写入 pressure 变量。",
            "成功读取后记录系统检查通过日志；不能引用未定义的节点。",
        ],
    },
    "run_pr_with_wait.group.json": {
        "title": "触发指定 PR 点位并等待完成",
        "requirements": [
            "pr_number 是必填的 u8 整数（0 至 255），timeout 是 u16 毫秒数，缺省 30000。",
            "先按 pr_number 触发运动，再用受支持的循环结构读取运动完成状态 motion_done。",
            "每轮读取状态并等待 100 ms，随后仅在 motion_done 不为 true 时继续；必须遵守所给超时。",
            "调用参数必须与声明相容；不得默默补造缺失的必填 PR 点号。",
        ],
    },
    "leak_test/judgment_stage.group.json": {
        "title": "按泄漏率阈值判定并记录结果",
        "requirements": [
            "当 leak_rate <= leak_rate_standard 时判定合格，否则不合格；示例输入为 0.1 和 0.5。",
            "合格时 test_result 必须为 PASS，亮绿灯，按产品编号和时间保存 PASS 记录并给出成功提示。",
            "不合格时 test_result 必须为 FAIL，亮红灯、蜂鸣 3 秒、保存 FAIL 记录并给出错误提示。",
            "条件表达式必须存在；结果变量、灯光、记录和提示应与同一次判定一致。",
        ],
    },
    "run_recipe.group.json": {
        "title": "按基线口味和规格运行十二阶段生产",
        "requirements": [
            "该基线仅提供三种口味 flavor=0..2，以及 specification=0 的 2 L、specification=1 的 330 mL。",
            "未配置的规格（包括 specification=2 的 250 mL）必须拒绝，不能在缺少配方时继续设备操作。",
            "按查找表选择配方，初始化计数和轴点位缓存；初始化一次、生产两批每批六个容器、CIP 清洗一次、归位待机一次。",
            "每个容器依次进行取空瓶、位置检查、冲洗、灌装定位、灌装、液位复检、封盖、打码贴标、成品下料。",
            "口味决定灌装设备；规格决定空瓶/成品点位及灌装时间；普通移动只触发目标 PR 点号发生变化的轴。",
        ],
    },
    "trigger_motor.group.json": {
        "title": "通过设备表触发电机 PR 点位",
        "requirements": [
            "motor_device 必须对应设备注册表中的电机；motor_pr 为预先配置的 PR 点号，本任务范围 0..15。",
            "用已定义的 encode_pr_trigger 节点将点号编码，再向指定电机发送触发命令。",
            "示例有效调用为 X 电机的 PR1；不允许构造不存在的设备或节点，也不在生产流程中重设 PR 参数。",
        ],
    },
    "step_11_cip_cleaning_sanitizing.group.json": {
        "title": "完成既定时长和顺序的 CIP 清洗",
        "requirements": [
            "先开启 CIPValve 的 0x0013 和 Relay 的 0x000e，再等待 120000 ms 循环清洗；等待不得移到开启之前或任意缩短。",
            "循环清洗后检查压力达到原配方要求 0.3 MPa，开启 0x0014 排水并等待 30000 ms，然后停止排水和循环。",
            "开启 RinseValve 的 0x0012，清水冲洗 20000 ms，随后关闭清水和 CIP 阀并更新完成状态。",
            "判定时关注开关顺序、持续时间及最终关闭状态，而不只判断配置是否能运行结束。",
        ],
    },
    "step_07_level_recheck.group.json": {
        "title": "四路液位独立复检并缓存",
        "requirements": [
            "在 LevelSensor 上依次轮询 0x0038、0x0039、0x003a、0x003b 四个不同寄存器，不能遗漏其中任何一路。",
            "各路未就绪时每 50 ms 重试，使用 3000 ms 超时及对应 RECHECK1_FAIL 至 RECHECK4_FAIL 错误。",
            "四路均通过后再次读取并分别保存 lv1、lv2、lv3、lv4，最后将 CheckState 更新为 qualified。",
            "其他设备上的同地址读数不能替代指定液位传感器；后续缓存读取不能替代前面的合格判定。",
        ],
    },
}


def _requirement(source: str) -> dict[str, Any]:
    name = source.split("/L3_group/", 1)[1]
    if name not in REQUIREMENTS:
        raise ReviewError("No reviewed process requirement for " + source)
    return deepcopy(REQUIREMENTS[name])


def _strings(value: Any):
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for child in value.values():
            yield from _strings(child)
    elif isinstance(value, list):
        for child in value:
            yield from _strings(child)


def _support(root: Path, request: dict[str, Any]) -> list[dict[str, Any]]:
    """Supply driver definitions and transitive L3 dependencies, not its answer."""
    layer = root / request["roots"][0]
    source = (root / request["source_path"]).resolve()
    paths = set()
    contracts = {}
    for directory in ("L1_action", "L2_node"):
        paths.update((layer / directory).rglob("*.json"))
    pending = [request["configuration"], request["params"]]
    while pending:
        for text in _strings(pending.pop()):
            if not text.startswith("L3_group/") or "${" in text:
                continue
            reference = text.split(".json", 1)[0] + ".json"
            target = (layer / reference).resolve()
            if layer.resolve() not in target.parents:
                raise ReviewError("Dependency escapes layer directory: " + text)
            if target == source or target in paths or not target.is_file():
                continue
            relative = target.relative_to(layer).as_posix()
            group_name = relative.split("/L3_group/", 1)[-1] if "/L3_group/" in relative else relative.removeprefix("L3_group/")
            if group_name in REQUIREMENTS:
                # Another candidate can depend on a reviewed group's original.
                # Share its declared interface/contract, not an answer copy that
                # would unblind that other candidate elsewhere in the packet.
                dependency = read(target)
                contracts[relative] = {"path": relative, "kind": "declared_dependency_contract",
                                       "args": dependency.get("args", []),
                                       "contract": deepcopy(REQUIREMENTS[group_name])}
                continue
            paths.add(target)
            pending.append(read(target))
    return ([{"path": path.relative_to(layer).as_posix(), "definition": read(path)}
             for path in sorted(paths)] + [contracts[name] for name in sorted(contracts)])


LANGUAGE_RULES = [
    "L3 使用 type=group；顺序体为 body；模式包括 sequence、parallel、loop、if、switch。",
    "循环使用 loop.type=while/do-while/count 与 body；while 在体前判断，do-while 在体后判断。",
    "节点使用 type=node 和 template（或 node.template）引用 L2，params 按位置传参；组可内联或用 template 引用 L3。",
    "args 声明类型、默认值及约束；结果写入共享变量，${key} 读取已存在的变量。未声明变量、失效引用或错误参数不能视为满足要求。",
    "本次审核目标是该配置及其调用是否满足所附工艺要求；即使当前解析器容忍某种写法或模拟器运行成功，也不能据此忽略要求。",
]


def _form(packet_id: str, role: str, candidates: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "schema_version": SCHEMA_VERSION,
        "packet_id": packet_id,
        "reviewer_role": role,
        "reviewer": {
            "reviewer_id": "", "name_optional": "", "process_control_experience": "",
            "is_real_human": None, "independent_review": None,
            "saw_reference_key": None, "discussed_decisions_before_submission": None,
            "involved_in_generation_or_repair": None,
        },
        "reviews": [{"candidate_id": item["candidate_id"],
                     "candidate_sha256": digest(item), "decision": None,
                     "explanation": "", "evidence": [], "reviewed_at_utc": None,
                     "elapsed_seconds": None}
                    for item in candidates],
    }


PACKET_INSTRUCTIONS = """独立人工语义审核

请仅接收本 reviewer_packet 文件夹和分配给您的 reviewer_a.json 或 reviewer_b.json。
不要查看 coordinator_only、源码缺陷生成器、其他人的表单、自动测试结果或参考答案。
本材料隐藏了原始缺陷编号、修改历史和参考判定，但不同候选可能来自同一工艺，
不能据此声称完全消除了学习或识别效应。每个候选应单独对照其要求判断。

逐个阅读 candidates/Hxxx.json。其中包括工艺要求、语言规则、候选配置及调用参数、
初始变量、设备表（如适用）和支持定义。支持定义不包含任何被审核顶层组的原始版本；
对这些组的间接调用改提供只读依赖契约，审核时将该依赖视为满足所声明的契约。
L1/L2 定义和所提供的依赖契约是本次审核的可信前提；评估候选 L3 及其调用，
不扩展为对驱动实现或真实工厂设备的独立安全认证。
accept 表示满足全部给定要求；reject 表示至少一项明确不满足。不要执行物理设备。
若定义不够清楚，保留 decision=null 并说明疑问，由协调人澄清；不得猜测并强行填满。

请如实填写人员 ID、相关经验和声明。每条需填写 decision、explanation、非空 evidence
列表、实际用时 elapsed_seconds（秒）以及真实审核时间 reviewed_at_utc，例如
2026-10-03T10:00:00Z。evidence 应指出具体
字段/节点、支持定义和对应要求；不要填写自动检测结果作为自己的独立人工意见。
请独立完成全部判断，再交回表单。A 可以是原作者；B 必须未参与这些配置的生成或修复。
禁止由脚本或语言模型代填决策。表单交回以前，不交流个案决定、不查看参考答案。

这是人工审核材料，不是审核结果；没有两份真实完整表单时不能报告一致率或 kappa。
"""


def prepare(output: str | Path, seed: int = DEFAULT_SEED,
            root: str | Path = ROOT) -> dict[str, Any]:
    root = Path(root).resolve()
    output = validate_output(output, root)
    cases = build_cases(root)
    pool, controls = [], {}
    for case in cases:
        control = case["control_request"]
        source = control["source_path"]
        key = digest(control)
        controls.setdefault(key, {"request": control, "expected_decision": "accept",
                                  "source_path": source, "source_case_ids": []})["source_case_ids"].append(case["id"])
        pool.append({"request": case["faulty_request"], "expected_decision": "reject",
                     "source_path": source, "source_case_ids": [case["id"]],
                     "reference_reason": case["description"], "reference_repair": case["repair"]})
    if len(controls) != 8 or len(cases) != 18:
        raise ReviewError("This protocol requires its documented 8 controls and 18 defects; review changed inputs first.")
    pool.extend(controls.values())
    random.Random(seed).shuffle(pool)
    candidates, answers = [], []
    for index, entry in enumerate(pool, 1):
        request = entry["request"]
        candidate = {
            "candidate_id": f"H{index:03d}",
            "requirement": _requirement(entry["source_path"]),
            "language_rules": LANGUAGE_RULES,
            "configuration_kind": request["kind"],
            "configuration": request["configuration"], "params": request["params"],
            "initial_variables": request["variables"],
            "device_registry": request.get("device_registry"),
            "context": "静态人工工艺审核；设备与点位已调试。不要连接物理设备。该材料不提供自动检查结论。",
            "supporting_definitions": _support(root, request),
        }
        candidates.append(candidate)
        answers.append({**{k: v for k, v in entry.items() if k != "request"},
                        "candidate_id": candidate["candidate_id"], "candidate_sha256": digest(candidate)})
    packet_id = digest(candidates)
    manifest = {"schema_version": SCHEMA_VERSION, "packet_id": packet_id,
                "candidate_count": len(candidates),
                "candidates": [{"candidate_id": c["candidate_id"], "sha256": digest(c),
                                "file": "candidates/" + c["candidate_id"] + ".json"} for c in candidates]}
    key_data = {"schema_version": SCHEMA_VERSION, "packet_id": packet_id,
                "coordinator_only": True, "seed": seed,
                "label_status": "Protocol reference expectations, not human consensus or measured review outcomes.",
                "reference_counts": {"accept": 8, "reject": 18}, "answers": answers}
    files = {output / "reviewer_packet/README.txt": PACKET_INSTRUCTIONS,
             output / "reviewer_packet/manifest.json": encoded(manifest),
             output / "coordinator_only/reference_key.json": encoded(key_data)}
    for candidate in candidates:
        files[output / "reviewer_packet/candidates" / (candidate["candidate_id"] + ".json")] = encoded(candidate)
    # Preflight all immutable artifacts before writing anything; completed and
    # partially completed forms are retained verbatim on an identical rerun.
    for path, content in files.items():
        if path.exists() and path.read_text(encoding="utf-8").replace("\r\n", "\n") != content:
            raise ReviewError("Existing artifact differs; choose a new output directory: " + str(path))
    forms = {}
    for role in ("A", "B"):
        path = output / ("reviewer_" + role.lower() + ".json")
        form = _form(packet_id, role, candidates)
        if path.exists():
            previous = read(path)
            if (previous.get("packet_id") != packet_id or previous.get("reviewer_role") != role
                    or [(row.get("candidate_id"), row.get("candidate_sha256")) for row in previous.get("reviews", [])]
                    != [(row["candidate_id"], row["candidate_sha256"]) for row in form["reviews"]]):
                raise ReviewError("Existing reviewer form does not match this packet; preserved: " + str(path))
        else:
            forms[path] = encoded(form)
    for path, content in {**files, **forms}.items():
        if not path.exists():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8", newline="\n")
    return {"status": "PREPARED_NOT_REVIEWED", "packet_id": packet_id,
            "candidate_count": len(candidates), "output": str(output),
            "note": "No human decisions or agreement measurements have been produced."}


def _check_form(form: Any, key: dict[str, Any], role: str) -> dict[str, str]:
    if not isinstance(form, dict) or form.get("schema_version") != SCHEMA_VERSION:
        raise ReviewError(role + ": unsupported reviewer form")
    if form.get("packet_id") != key["packet_id"] or form.get("reviewer_role") != role:
        raise ReviewError(role + ": packet or reviewer role mismatch")
    reviewer = form.get("reviewer", {})
    for field in ("reviewer_id", "process_control_experience"):
        if not isinstance(reviewer.get(field), str) or not reviewer[field].strip():
            raise ReviewError(role + ": complete the real reviewer's " + field)
    for field in ("is_real_human", "independent_review"):
        if reviewer.get(field) is not True:
            raise ReviewError(role + ": a real independent human reviewer must attest " + field)
    for field in ("saw_reference_key", "discussed_decisions_before_submission"):
        if reviewer.get(field) is not False:
            raise ReviewError(role + ": blinded independent review requires " + field + "=false")
    if type(reviewer.get("involved_in_generation_or_repair")) is not bool:
        raise ReviewError(role + ": declare generation/repair involvement")
    if role == "B" and reviewer["involved_in_generation_or_repair"]:
        raise ReviewError("B must be an independent reviewer who did not generate or repair these configurations")
    expected = {row["candidate_id"]: row for row in key["answers"]}
    rows = form.get("reviews")
    if not isinstance(rows, list) or len(rows) != len(expected):
        raise ReviewError(role + ": all candidate reviews are required; no measured report was generated")
    decisions = {}
    for row in rows:
        if not isinstance(row, dict):
            raise ReviewError(role + ": malformed review row")
        candidate = row.get("candidate_id")
        if candidate not in expected or candidate in decisions:
            raise ReviewError(role + ": unexpected or duplicate candidate ID")
        if row.get("candidate_sha256") != expected[candidate]["candidate_sha256"]:
            raise ReviewError(role + ": candidate hash mismatch for " + candidate)
        if row.get("decision") not in ("accept", "reject"):
            raise ReviewError(role + ": incomplete decision for " + candidate + "; no measured report was generated")
        if not isinstance(row.get("explanation"), str) or not row["explanation"].strip():
            raise ReviewError(role + ": missing explanation for " + candidate)
        evidence = row.get("evidence")
        if not isinstance(evidence, list) or not evidence or not all(isinstance(e, str) and e.strip() for e in evidence):
            raise ReviewError(role + ": missing concrete evidence for " + candidate)
        elapsed = row.get("elapsed_seconds")
        if type(elapsed) not in (int, float) or not math.isfinite(elapsed) or elapsed <= 0:
            raise ReviewError(role + ": supply positive finite actual elapsed_seconds for " + candidate)
        stamp = row.get("reviewed_at_utc")
        try:
            parsed = datetime.fromisoformat(stamp.replace("Z", "+00:00"))
            if parsed.utcoffset() is None or parsed.utcoffset().total_seconds() != 0:
                raise ValueError("not UTC")
        except (ValueError, TypeError, AttributeError):
            raise ReviewError(role + ": supply a real UTC review time for " + candidate) from None
        decisions[candidate] = row["decision"]
    return decisions


def _confusion(decisions: dict[str, str], references: dict[str, str]) -> dict[str, Any]:
    matrix = {"true_reject": 0, "false_reject": 0, "false_accept": 0, "true_accept": 0}
    for candidate, expected in references.items():
        actual = decisions[candidate]
        matrix[("true_" if actual == expected else "false_") + actual] += 1
    rejects = sum(value == "reject" for value in references.values())
    accepts = len(references) - rejects
    return {"positive_label": "reject (reference-invalid configuration)", **matrix,
            "reference_reject_count": rejects, "reference_accept_count": accepts,
            "reject_recall": matrix["true_reject"] / rejects if rejects else None,
            "false_rejection_rate": matrix["false_reject"] / accepts if accepts else None,
            "accuracy_against_reference": (matrix["true_reject"] + matrix["true_accept"]) / len(references)}


def score(reviewer_a: str | Path, reviewer_b: str | Path, key: str | Path,
          output: str | Path, root: str | Path = ROOT) -> dict[str, Any]:
    output = validate_output(output, root)
    a, b, reference = read(reviewer_a), read(reviewer_b), read(key)
    if (not isinstance(reference, dict) or reference.get("schema_version") != SCHEMA_VERSION
            or reference.get("coordinator_only") is not True):
        raise ReviewError("Invalid coordinator reference key")
    answers = reference.get("answers", [])
    if (len(answers) != 26 or len({x.get("candidate_id") for x in answers}) != 26
            or any(x.get("expected_decision") not in ("accept", "reject") for x in answers)):
        raise ReviewError("Reference key must contain the 26 unique candidate expectations")
    references = {row["candidate_id"]: row["expected_decision"] for row in answers}
    if sum(value == "accept" for value in references.values()) != 8:
        raise ReviewError("Reference key must contain 8 valid controls and 18 defects")
    da, db = _check_form(a, reference, "A"), _check_form(b, reference, "B")
    if a["reviewer"]["reviewer_id"].strip().casefold() == b["reviewer"]["reviewer_id"].strip().casefold():
        raise ReviewError("Two distinct independent human reviewers are required")
    count = len(references)
    agree = sum(da[candidate] == db[candidate] for candidate in references)
    pa = sum(value == "accept" for value in da.values()) / count
    pb = sum(value == "accept" for value in db.values()) / count
    observed = agree / count
    chance = pa * pb + (1 - pa) * (1 - pb)
    kappa = (observed - chance) / (1 - chance) if chance < 1 else None
    disagreements = [{"candidate_id": candidate, "reviewer_a": da[candidate],
                      "reviewer_b": db[candidate], "reference_expectation": references[candidate]}
                     for candidate in sorted(references) if da[candidate] != db[candidate]]
    result = {
        "schema_version": SCHEMA_VERSION, "status": "SCORED_COMPLETED_HUMAN_FORMS",
        "packet_id": reference["packet_id"], "candidate_count": count,
        "reviewer_ids": {"A": a["reviewer"]["reviewer_id"], "B": b["reviewer"]["reviewer_id"]},
        "input_sha256": {"reviewer_a": digest(a), "reviewer_b": digest(b), "reference_key": digest(reference)},
        "raw_agreement_count": agree, "raw_agreement": observed,
        "chance_agreement": chance, "cohen_kappa": kappa,
        "kappa_note": "Undefined when both reviewers use one identical decision for every item." if kappa is None else None,
        "reference_comparison": {"A": _confusion(da, references), "B": _confusion(db, references)},
        "decision_counts": {"A": {"accept": sum(v == "accept" for v in da.values()), "reject": sum(v == "reject" for v in da.values())},
                            "B": {"accept": sum(v == "accept" for v in db.values()), "reject": sum(v == "reject" for v in db.values())}},
        "review_time_seconds": {
            role: {"total": sum(row["elapsed_seconds"] for row in form["reviews"]),
                   "median_per_candidate": statistics.median(row["elapsed_seconds"] for row in form["reviews"])}
            for role, form in (("A", a), ("B", b))},
        "disagreements": disagreements,
        "per_candidate": [{"candidate_id": candidate, "reviewer_a": da[candidate], "reviewer_b": db[candidate],
                           "reference_expectation": references[candidate]} for candidate in sorted(references)],
        "limitations": [
            "Human identity, expertise, independence and completion times are reviewer self-attestations; this script cannot authenticate them.",
            "Reference expectations are defined by this defect protocol, not independent consensus or universal ground truth.",
            "Related candidates from eight source groups are not statistically independent or a representative deployment sample.",
            "Calls to other reviewed top-level groups are assessed against declared dependency contracts; their pristine definitions are withheld to reduce answer leakage.",
            "This is a human-review agreement study, not a measurement of physical safety or of automatic gate detection.",
        ],
    }
    content = encoded(result)
    if output.exists() and output.read_text(encoding="utf-8").replace("\r\n", "\n") != content:
        raise ReviewError("Existing result differs; choose a new output file: " + str(output))
    if not output.exists():
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(content, encoding="utf-8", newline="\n")
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    prep = sub.add_parser("prepare", help="Create blinded packets and blank forms; no measured result")
    prep.add_argument("--output", type=Path, default=ROOT / "build/semantic-review")
    prep.add_argument("--seed", type=int, default=DEFAULT_SEED)
    scoring = sub.add_parser("score", help="Score only two completed real independent human forms")
    scoring.add_argument("--reviewer-a", required=True, type=Path)
    scoring.add_argument("--reviewer-b", required=True, type=Path)
    scoring.add_argument("--key", required=True, type=Path)
    scoring.add_argument("--output", type=Path, default=ROOT / "build/semantic-review/results.json")
    args = parser.parse_args(argv)
    try:
        if args.command == "prepare":
            result = prepare(args.output, args.seed)
            print(encoded(result), end="")
        else:
            result = score(args.reviewer_a, args.reviewer_b, args.key, args.output)
            print(encoded({key: result[key] for key in
                           ("status", "candidate_count", "raw_agreement", "cohen_kappa")}), end="")
        return 0
    except (ReviewError, OSError, ValueError, KeyError, TypeError) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
