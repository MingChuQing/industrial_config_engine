"""Single-change configuration defects for the real-executor gate experiment.

The unmodified checked-in L3 groups are controls, and every repair restores that
same document and invocation.  This module assigns no expected detector: the
probe measures structural validation and execution, while a separate comparison
oracle can expose accepted changes to the required behavior.  An oracle finding
is not an engine-gate detection or an independent human review.
"""

from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
from typing import Any


def build_cases(root: str | Path) -> list[dict[str, Any]]:
    """Return independently runnable control/defect/restoration requests.

    ``root`` is the repository directory used only to read fixtures.  Paths in
    requests are repository-relative; run the C++ probe with that directory as
    its working directory, avoiding Windows narrow-string Unicode path issues.
    """
    root = Path(root)
    cases: list[dict[str, Any]] = []

    def read(relative: str) -> Any:
        return json.loads((root / relative).read_text(encoding="utf-8-sig"))

    def request(example: str, source: str, profile: str,
                params: list[Any] | None = None,
                variables: dict[str, Any] | None = None) -> dict[str, Any]:
        base = "examples/" + example
        beverage = example.startswith("beverage_full/")
        layer = base + "/layered" if beverage else base
        values = read(base + "/system_variables.json") if beverage else {}
        values.update(variables or {})
        result: dict[str, Any] = {
            "source_path": layer + "/L3_group/" + source,
            "roots": [layer],
            "kind": "group",
            "configuration": read(layer + "/L3_group/" + source),
            "params": params if params is not None else [],
            "variables": values,
            "profile": profile,
            "step_budget": 50000,
        }
        if beverage:
            result["device_registry"] = read(base + "/device_registry.json")
            result["beverage_volume_ml"] = 2000
        return result

    def add(case_id: str, example: str, category: str, description: str,
            repair: str, control: dict[str, Any], path: list[Any],
            replacement: Any = None, *, remove: bool = False) -> None:
        faulty = deepcopy(control)
        cursor: Any = faulty
        for key in path[:-1]:
            cursor = cursor[key]
        if remove:
            del cursor[path[-1]]
        else:
            cursor[path[-1]] = replacement
        cases.append({
            "id": case_id,
            "example": example,
            "category": category,
            "source_path": control["source_path"],
            "description": description,
            "repair": repair,
            "mutation_path": path,
            "control_request": deepcopy(control),
            "faulty_request": faulty,
            "fixed_request": deepcopy(control),
        })

    cfg = "configuration"
    vacuum = request("example1", "vacuum/pump_down.group.json", "vacuum")
    add("T1-schema", "example1", "schema",
        "首个节点的类型从 node 改为不支持的 operation。",
        "把 body[0].type 恢复为 node。", vacuum,
        [cfg, "body", 0, "type"], "operation")
    add("T1-variable", "example1", "undefined-variable",
        "抽真空循环条件把压力变量 pressure 拼错为 pressue。",
        "将条件中的变量名恢复为 pressure，与读取节点的输出一致。", vacuum,
        [cfg, "body", 3, "loop", "condition"], "${pressue} > 5")
    add("T1-loop", "example1", "nonterminating-loop",
        "压力循环退出条件改为恒真，达到目标压力后仍不能正常退出。",
        "恢复条件 '${pressure} > 5'，保留原有超时限制。", vacuum,
        [cfg, "body", 3, "loop", "condition"], "1 == 1")
    system_check = request("example1", "common/system_check.group.json", "vacuum")
    add("T1-reference", "example1", "missing-reference",
        "系统检查引用不存在的压力读取节点。",
        "恢复原文件中的 read_pressure 节点引用。", system_check,
        [cfg, "body", 0, "template"],
        "L2_node/vacuum/all_nodes.json/read_pressue.r_u16_pressure")

    feeding = request("example2", "run_pr_with_wait.group.json", "feeding", [1, 30000])
    add("T2-loop-schema", "example2", "loop-schema",
        "等待运动完成的内层循环使用不支持的 repeat-until 类型。",
        "将 loop.type 恢复为 do-while，先读完成状态再判断。", feeding,
        [cfg, "body", 1, "loop", "type"], "repeat-until")
    add("T2-arity", "example2", "missing-argument",
        "调用时省略必填的 PR 点号（超时参数有默认值）。",
        "恢复调用参数 [1,30000]，明确点号与超时。", feeding, ["params"], [])
    add("T2-type", "example2", "argument-type",
        "把 u8 类型的 PR 点号传成非数字字符串。",
        "将点号恢复为数值 1。", feeding, ["params", 0], "not-a-number")
    add("T2-range", "example2", "argument-range",
        "传入 PR 点号 256，超出声明的 u8 类型范围。",
        "将点号恢复为原配置使用的 1。", feeding, ["params", 0], 256)

    judgment = request("example3", "leak_test/judgment_stage.group.json", "leak",
                       [0.5, "DEFECT-001"],
                       {"leak_rate": 0.1, "timestamp": "2026-10-02T00:00:00Z"})
    add("T3-predicate", "example3", "wrong-predicate",
        "将泄漏率合格判据反转，使 0.1 <= 0.5 的合格样本走失败分支。",
        "恢复 '${leak_rate} <= ${leak_rate_standard}'。", judgment,
        [cfg, "condition", "expression"], "${leak_rate} > ${leak_rate_standard}")
    add("T3-condition-schema", "example3", "condition-schema",
        "删除 if 条件对象中必需的 expression。",
        "恢复原有泄漏率比较表达式。", judgment,
        [cfg, "condition", "expression"], remove=True)
    add("T3-result", "example3", "wrong-result",
        "合格分支把结果写成 FAIL，但仍亮绿灯并记录 PASS。",
        "把合格分支的结果参数恢复为 PASS，使结果与后续动作一致。", judgment,
        [cfg, "then", 0, "params", 0], "FAIL")

    baseline = request("beverage_full/example4", "run_recipe.group.json", "beverage", [0, 0])
    add("B4-range", "beverage_full/example4", "declared-constraint",
        "基线只支持规格 0、1，却请求尚未支持的规格 2（250 mL）。",
        "恢复基线支持的规格 0（2 L）；250 mL 应使用扩展样例。", baseline, ["params", 1], 2)
    motor = request("beverage_full/example4", "trigger_motor.group.json", "beverage", ["X", 1])
    add("B4-device", "beverage_full/example4", "unknown-device",
        "电机子过程绑定到设备表中不存在的设备。",
        "将设备参数恢复为设备表已定义的 X。", motor, ["params", 0], "UnregisteredMotor")
    add("B4-reference", "beverage_full/example4", "missing-reference",
        "电机子过程调用不存在的 PR 触发编码节点。",
        "恢复原有 encode_pr_trigger 节点引用。", motor,
        [cfg, "body", 0, "template"],
        "L2_node/all_nodes.json/encode_missing_pr.u16.r_u16_trigger_word")

    cip = request("beverage_full/example5", "step_11_cip_cleaning_sanitizing.group.json", "beverage")
    add("B5-duration", "beverage_full/example5", "insufficient-duration",
        "将案例规定的 CIP 循环清洗时间从 120000 ms 缩短为 1000 ms。",
        "将循环清洗等待恢复为 120000 ms。", cip,
        [cfg, "body", 3, "params", 0], 1000)
    reordered = deepcopy(cip["configuration"]["body"])
    reordered[1], reordered[3] = reordered[3], reordered[1]
    add("B5-order", "beverage_full/example5", "wrong-order",
        "交换开启 CIP 阀与循环等待的位置，导致等待发生在开阀之前。",
        "恢复先开阀、再等待的原有工序顺序。", cip, [cfg, "body"], reordered)
    level = request("beverage_full/example5", "step_07_level_recheck.group.json", "beverage")
    add("B5-omission", "beverage_full/example5", "omitted-check",
        "删除首个独立液位复检轮询，保留其它检查与缓存读取。",
        "恢复 body[1] 处对 0x0038 寄存器的轮询检查。", level,
        [cfg, "body", 1], remove=True)
    add("B5-device", "beverage_full/example5", "wrong-existing-device",
        "首个液位复检绑定到已存在但错误的 BottleSensor。",
        "将设备绑定恢复为 LevelSensor。", level,
        [cfg, "body", 1, "params", 1], "BottleSensor")

    return cases
