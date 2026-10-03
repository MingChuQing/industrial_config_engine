"""Extract the complete engineering supplement for the recorded 250 mL extension.

The candidate is reconstructed by applying the unchanged raw response to example4.
example5 is used only for its three frozen monolithic evaluator references. No LLM
is run and no historical input, response, provenance or configuration is edited.
"""
import argparse
import copy
import csv
import hashlib
import io
import json
from pathlib import Path
import shutil
import tempfile

import verify_beverage_nl_extension as replay
from build_beverage_full import old_steps, stage_plans, STAGES
from simulate_beverage_full import FAULTS

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "examples/beverage_nl_extension/process_spec"

STAGE_SUMMARIES = [{'stage': 1,
  'name_zh': '初始化',
  'stage_key': 'system_initialization',
  'frequency': '每配方一次；位于2×6生产循环前',
  'point_route_fields': '无配方点位移动；显式触发X/Y1/Y2/Z各自PR0',
  'actions_zh': '关闭三个灌装头、夹爪、供盖器、冲洗阀、CIP阀/排水阀及Relay '
                '0x0008–0x000E；四定位电机PR0；轮询完成；保存last_*_pr=0；读坐标并更新UI；报警检查；缓存运行状态；稳定1秒。',
  'success_zh': '四轴完成位=1，报警寄存器0x2203均为0',
  'waits_zh': '四轴完成轮询20ms/4000ms；显式等待1000ms',
  'failure_codes_zh': 'HOME_TIMEOUT；X_ALARM/Y_ALARM/Y_ALARM_Y2/Z_ALARM',
  'notes_zh': '预置PR0移动，不是硬件限位寻零。',
  'source': 'example5/layered/L3_group/step_01_system_initialization.group.json'},
 {'stage': 2,
  'name_zh': '空瓶上料与夹取',
  'stage_key': 'empty_bottle_feeding',
  'frequency': '每瓶一次；每配方12次',
  'point_route_fields': 'empty_entry → empty_work',
  'actions_zh': 'Relay推瓶输出0x0008开；Infeed PR=infeed_pr；完成后确认上料BottleSensor '
                '0x0034；关推瓶；先入空瓶entry再work；Gripper线圈0x0000开；确认抓取0x0041。',
  'success_zh': '上料传感器=1且抓取传感器=1',
  'waits_zh': 'Infeed20ms/4000ms；上料100ms/5000ms；抓取50ms/3000ms；每次move四轴20ms/4000ms',
  'failure_codes_zh': 'EMPTY_FEED_TIMEOUT；BOTTLE_SENSOR_TIMEOUT；PICK_TIMEOUT；MOTION_TIMEOUT',
  'notes_zh': '夹爪在此夹住瓶子，直到阶段10松夹。',
  'source': 'example5/layered/L3_group/step_02_empty_bottle_feeding.group.json'},
 {'stage': 3,
  'name_zh': '瓶位确认',
  'stage_key': 'bottle_position_detection',
  'frequency': '每瓶一次；每配方12次',
  'point_route_fields': '保持empty_work，不新增移动',
  'actions_zh': '确认ContainerSensor FC04 0x0001；逐路确认BottleSensor 0x0034–0x0037并缓存；读取累计产量FlowMeter0x0020；UI '
                'BottleState=all ready。',
  'success_zh': 'ContainerSensor及四路状态均为1',
  'waits_zh': '容器20ms/2000ms；四路瓶位各50ms/3000ms',
  'failure_codes_zh': 'NO_CONTAINER；POS1_TIMEOUT/POS2_TIMEOUT/POS3_TIMEOUT/POS4_TIMEOUT',
  'notes_zh': '四路信号，不是四个需要搬运到的工位。',
  'source': 'example5/layered/L3_group/step_03_bottle_position_detection.group.json'},
 {'stage': 4,
  'name_zh': '瓶内冲洗',
  'stage_key': 'in_bottle_rinsing',
  'frequency': '每瓶一次；每配方12次',
  'point_route_fields': 'empty_entry → rinse_entry → rinse_work → rinse_entry',
  'actions_zh': '抬升到空瓶entry；转冲洗entry再下降work；冲洗阀0x0012开5s后关；压力检查0x0010>=0.2MPa；消毒阀CIP0x0013开3s后关；排水阀CIP0x0014开3s后关；UI '
                'WashState=done；抬升至rinse_entry。',
  'success_zh': '实际开阀时长分别>=5000/3000/3000ms；压力>=0.2MPa；保持夹持且位于冲洗work',
  'waits_zh': '显式等待5000+3000+3000ms；移动完成轮询20ms/4000ms',
  'failure_codes_zh': 'RINSE_PRESSURE_LOW；MOTION_TIMEOUT；模拟模型另检RINSE_DURATION/RINSE_SEQUENCE/PROCESS_POSITION',
  'notes_zh': '这些时长/阈值是已有模拟案例工程输入，不是由250mL容积推导或物理标定。',
  'source': 'example5/layered/L3_group/step_04_in_bottle_rinsing.group.json'},
 {'stage': 5,
  'name_zh': '灌装定位',
  'stage_key': 'filling_positioning',
  'frequency': '每瓶一次；每配方12次',
  'point_route_fields': 'fill_entry → fill_work；口味决定fill点位',
  'actions_zh': '先到选定口味灌装entry再work；BottleSensor0x0042–0x0045逐路检查到位。',
  'success_zh': '瓶已完成冲洗；四路灌装位置状态=1；选择配方对应灌装头',
  'waits_zh': '四路各50ms/3000ms；移动20ms/4000ms',
  'failure_codes_zh': 'FILL1_POS_TIMEOUT/FILL2_POS_TIMEOUT/FILL3_POS_TIMEOUT/FILL4_POS_TIMEOUT；MOTION_TIMEOUT',
  'notes_zh': '三口味分别OrangeFill/AppleFill/GrapeFill，按配方运行一个灌装头。',
  'source': 'example5/layered/L3_group/step_05_filling_positioning.group.json'},
 {'stage': 6,
  'name_zh': '定量灌装',
  'stage_key': 'filling',
  'frequency': '每瓶一次；每配方12次',
  'point_route_fields': '保持fill_work',
  'actions_zh': '清FlowMeter0x0001；给fill_device写容量0x0010及灌装时间0x0011；开灌装头线圈0x0000；等待完成；检查FlowMeter0x0000>=volume_ml；确认液位0x0038；关灌装头；4次PressureSensor0x0012>=0.1MPa；缓存流量及头部0x0012实灌量；更新UI。',
  'success_zh': '配方容量250mL；灌装时长橙1250/苹果1389/葡萄1137ms；累计量>=250；液位=1；4次压力>=0.1MPa',
  'waits_zh': '灌装完成20ms/20000ms；液位50ms/8000ms；4次压力检查无额外等待',
  'failure_codes_zh': 'FILL_TIMEOUT；FLOW_NOT_REACHED；LEVEL_TIMEOUT；FILL_PRESSURE_LOW；COMM_OFFLINE',
  'notes_zh': '已保存旧动作name包含“500ml”，但compareValue实际为配方250；表格应以字段值为准，不能复述过时name。',
  'source': 'example5/layered/L3_group/step_06_filling.group.json'},
 {'stage': 7,
  'name_zh': '液位复检',
  'stage_key': 'level_recheck',
  'frequency': '每瓶一次；每配方12次',
  'point_route_fields': '保持fill_work，无独立质检点位',
  'actions_zh': '独立轮询LevelSensor0x0038/39/3A/3B并缓存lv1–lv4；CheckState=qualified。',
  'success_zh': '四路液位均为1，模拟器记录4路已检查才允许封口',
  'waits_zh': '每路50ms/3000ms',
  'failure_codes_zh': 'RECHECK1_FAIL/RECHECK2_FAIL/RECHECK3_FAIL/RECHECK4_FAIL；模型另检LEVEL_NOT_VERIFIED',
  'notes_zh': '属于在线液位质检；无视觉、称重、泄漏测试、坏品分拣或独立质检搬运。',
  'source': 'example5/layered/L3_group/step_07_level_recheck.group.json'},
 {'stage': 8,
  'name_zh': '供盖与封口',
  'stage_key': 'capping',
  'frequency': '每瓶一次；每配方12次',
  'point_route_fields': 'fill_entry → closure_entry → closure_work → closure_entry',
  'actions_zh': '抬升离开灌装；去封口entry/work；CapFeeder开；Closure按closure_pr触发；等待完成；Relay0x000B封口输出开3s；读Torque0x0030>=2.0N·m；关封口输出；BottleSensor0x0046盖到位；缓存扭矩；CapFeeder关；抬升。',
  'success_zh': '闭合状态、四路液位通过、供盖器开启；封口保持>=3000ms且扭矩>=2.0N·m；盖到位=1',
  'waits_zh': 'Closure20ms/5000ms；封口显式3000ms；盖到位50ms/3000ms',
  'failure_codes_zh': 'CLOSURE_TIMEOUT；TORQUE_LOW；CAP_TIMEOUT；MOTION_TIMEOUT；模型另检CAPPING_DURATION/CLOSURE_SEQUENCE/CLOSURE_MODE',
  'notes_zh': '250mL使用closure_pr=3，closure_mode=screw_small是配方描述；真实机构校准未建模。',
  'source': 'example5/layered/L3_group/step_08_capping.group.json'},
 {'stage': 9,
  'name_zh': '喷码与贴标',
  'stage_key': 'coding_labeling',
  'frequency': '每瓶一次；每配方12次',
  'point_route_fields': 'code_entry → code_work → code_entry',
  'actions_zh': '进喷码entry/work；Relay0x000C喷码开/关；0x000D贴标开/关；BottleSensor0x0047完成确认；UI CodeState=done；抬升。',
  'success_zh': '瓶已封口且扭矩检查通过；喷码先于贴标；完成位=1',
  'waits_zh': '无显式脉冲timer；每次模拟请求5ms使开/关脉冲约5ms；完成确认50ms/3000ms',
  'failure_codes_zh': 'CODE_POS_TIMEOUT；MOTION_TIMEOUT；模型另检LABEL_SEQUENCE/LABEL_DURATION',
  'notes_zh': '没有码内容、标签版式或条码视觉检验参数；不能声称完整包装作业。',
  'source': 'example5/layered/L3_group/step_09_coding_labeling.group.json'},
 {'stage': 10,
  'name_zh': '成品放置与出料',
  'stage_key': 'finished_product_discharge',
  'frequency': '每瓶一次；每配方12次',
  'point_route_fields': 'finished_entry → finished_work → finished_entry',
  'actions_zh': '进入成品entry/work；Gripper关以松夹；Relay推瓶0x0008开；Outfeed '
                'PR=outfeed_pr；完成并确认BottleSensor0x0040；缓存累计产量；关推瓶；抬升；completed加1并更新UI。',
  'success_zh': '成品已灌装、封口、液位复检、喷码与贴标；松夹后出料完成位与传感器=1',
  'waits_zh': 'Outfeed20ms/4000ms；出料传感器100ms/5000ms；移动20ms/4000ms',
  'failure_codes_zh': 'OUTFEED_TIMEOUT；OUT_SENSOR_TIMEOUT；MOTION_TIMEOUT；模型另检UNFINISHED_RELEASE/UNLABELLED_RELEASE/UNLABELLED_OUTPUT',
  'notes_zh': '仅成品放置/出料；没有装箱、封箱、码垛、包装机或包装点位。',
  'source': 'example5/layered/L3_group/step_10_finished_product_discharge.group.json'},
 {'stage': 11,
  'name_zh': 'CIP清洗',
  'stage_key': 'cip_cleaning_sanitizing',
  'frequency': '每配方一次；完成全部12瓶后',
  'point_route_fields': '不新增生产点位移动；保持最后finished_entry',
  'actions_zh': 'CIP0x0013开，Relay泵0x000E开；循环120s；压力0x0010>=0.3MPa；CIP排水0x0014开30s后关；停泵；冲洗阀0x0012开20s后关；关CIP0x0013；UI '
                'CIPState=done。',
  'success_zh': '全部12瓶已出料且无持瓶；循环>=120000ms、排水>=30000ms、清水>=20000ms；压力>=0.3MPa；停泵后清水',
  'waits_zh': '显式120000+30000+20000ms',
  'failure_codes_zh': 'CIP_PRESSURE_LOW；模型另检CIP_SEQUENCE/CIP_DURATION/CIP_INCOMPLETE',
  'notes_zh': '模拟清洗顺序和时长检查，不是卫生效果或食品安全验证。',
  'source': 'example5/layered/L3_group/step_11_cip_cleaning_sanitizing.group.json'},
 {'stage': 12,
  'name_zh': '回原点与待机',
  'stage_key': 'homing_standby',
  'frequency': '每配方一次；CIP完成后',
  'point_route_fields': '显式触发X/Y1/Y2/Z各自PR0',
  'actions_zh': '重复复位所有输出；四轴显式PR0并等待完成；记录last_*_pr=0；刷新位置UI；四轴报警检查；SystemState=standby。',
  'success_zh': 'CIP完成；定位四轴PR0；无待完成动作、无持瓶且所有输出关闭；报警=0',
  'waits_zh': '四轴20ms/4000ms；无额外稳定timer',
  'failure_codes_zh': 'HOME_TIMEOUT；X_ALARM/Y_ALARM/Y_ALARM_Y2/Z_ALARM；模型另检FINAL_STATE/HOME_POSITION',
  'notes_zh': '与初始化一样，PR0不受“点号未变化则跳过”优化影响。',
  'source': 'example5/layered/L3_group/step_12_homing_standby.group.json'}]

DEVICE_ROLES = {'X': 'X定位电机',
 'Y1': 'Y轴第一定位电机',
 'Y2': 'Y轴第二定位电机（与Y1同PR号，单独设备）',
 'Z': 'Z定位电机',
 'Infeed': '空容器进料电机',
 'Closure': '封口机构电机',
 'Outfeed': '成品出料电机',
 'OrangeFill': '橙汁灌装头',
 'AppleFill': '苹果汁灌装头',
 'GrapeFill': '葡萄汁灌装头',
 'Gripper': '夹瓶器',
 'CapFeeder': '供盖器',
 'ContainerSensor': '持瓶状态传感器',
 'RinseValve': '冲洗阀0x0012',
 'CIPValve': '消毒/CIP阀0x0013及排水阀0x0014',
 'Relay': '推瓶0x0008、封口0x000B、喷码0x000C、贴标0x000D、CIP泵0x000E（0x0009/0x000A仅复位）',
 'BottleSensor': '上料/抓取/瓶位/灌装位置/瓶盖/喷码贴标/出料反馈',
 'LevelSensor': '四路液位0x0038–0x003B',
 'PressureSensor': '冲洗/CIP压力0x0010与灌装压力0x0012',
 'FlowMeter': '0x0001清零、0x0000流量、0x0020累计产量',
 'Torque': '封口扭矩0x0030'}

LIMITATIONS = ['No packaging, boxing, carton sealing or palletizing operation, device or production point is present. Finished '
 'discharge is the end of the implemented container flow.',
 'Level recheck occurs in place at fill_work; no separate quality-inspection move exists. Torque and cap checks happen '
 'at closure_work, coding/label signal at code_work.',
 'Y1/Y2 are two devices assigned the same y PR number. Trigger writes are issued sequentially then each motor is '
 'polled; this is not a hardware synchronization claim.',
 'Production point index 0..48 maps to precommissioned motor PR numbers 0..15. Values are not coordinates in '
 'millimeters. The virtual position readback PR*1000 is synthetic.',
 'Code carries no physical PR location, speed, acceleration, coordinate calibration, collision envelope, gearing, Y '
 'gantry squaring or emergency-stop implementation.',
 'On check failure the simulator emits error and raises SimulationError, ending the scenario. There is no modeled '
 'global safe-stop/recovery transaction sequence; do not promise every valve is shut on failure.',
 'Original context.json supplies all baseline points/recipes, run_recipe and L4 entrances, plus frozen dependency '
 'description. It does not embed all twelve L3 stage bodies. Added comprehensive workbook is retrospective engineering '
 'documentation, not original recorded model input.',
 'Original stage-6 flow-check name says 500ml; actual compareValue correctly resolves from each recipe (250 in this '
 'extension). Document actual fields, not stale action label.',
 'Coding/labeling use adjacent ON/OFF writes with no explicit timer. The 5ms duration follows the deterministic model '
 'transaction time, not a physical actuator requirement.',
 'New size uses synthetic engineering data and existing process, not a method for inferring industrial process plans '
 'from product volume alone.',
 'The two batches and six containers per batch are literal L3 loop counts, not dynamically read from recipe.batches or '
 'units_per_batch. The recorded recipe metadata is consistent with those fixed counts; changing those metadata alone '
 'would not change the production loops.']

MOVEMENT_CONTRACT = {'production_point_range_declared': [0, 100],
 'point_count_before': 33,
 'point_count_after': 49,
 'motor_pr_range': [0, 15],
 'trigger_register': '0x6002',
 'trigger_value': '16 + pr_number',
 'motion_poll': 'FC04 register0 expected 0001; 20ms retry, 4000ms deadline',
 'position_readback': 'FC03 0x602C length2 registers; uint32/int big endian in model',
 'ui_mapping': {'centerX': 'X_position', 'centerY': 'Y1_position', 'centerZ': 'Z_position'},
 'axis_issue_order': ['X', 'Y1', 'Y2', 'Z'],
 'no_change_policy': 'Ordinary move triggers only changed logical axis; Y changes trigger both Y1 and Y2. All four are '
                     'still polled and all four positions read. Init/standby explicitly trigger PR0.'}

def compact(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


def lines(document):
    return len(json.dumps(document, ensure_ascii=False, indent=4).splitlines())


def digest(path):
    # Match the archived replay policy: normalize only Windows line endings.
    return hashlib.sha256(Path(path).read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def scalar(value):
    if value is None:
        return ""
    if isinstance(value, (dict, list, tuple)):
        return compact(value)
    return value


def sheet(name, title, note, headers, rows, widths):
    if len(headers) != len(widths):
        raise ValueError("Column width mismatch: " + name)
    values = [[scalar(c) for c in row] for row in rows]
    if any(len(row) != len(headers) for row in values):
        raise ValueError("Row width mismatch: " + name)
    return dict(name=name, title=title, note=note, headers=headers, rows=values, widths=widths)


def frozen_sources():
    provenance = replay.read(replay.INPUT / "provenance.json")
    norm = provenance["normalized_sha256"]
    for name, value in provenance["input_sha256"].items():
        replay.frozen_hash(replay.INPUT/name, value, norm["inputs"][name])
    for name, value in provenance["baseline_sha256"].items():
        replay.frozen_hash(replay.BASELINE/name, value, norm["baseline"][name])
    replay.frozen_hash(replay.INPUT/"raw_response.json", provenance["raw_response_sha256"], norm["raw_response"])
    for flavor in replay.REFERENCE_SHA256:
        replay.frozen_hash(replay.REFERENCE/(flavor+"_small.json"),
                           replay.REFERENCE_SHA256[flavor], replay.REFERENCE_NORMALIZED_SHA256[flavor])
    return provenance


def operation_record(recipe, points, stage, number, operation):
    kind, *args = operation
    r = dict(recipe=recipe["id"], stage=stage, step=number, kind=kind, raw_plan=operation,
             frequency="每配方一次" if stage in (1, 11, 12) else "每瓶一次；每配方12次",
             point="", target="", device="", action="", wait="", success="", failure="")
    if kind == "legacy":
        a = args[0]
        r["action_fields"] = a
        r["device"] = a.get("device", "")
        r["target"] = a.get("request", "")
        r["action"] = a.get("name", a["type"])
        if a["type"] == "autorequestresponse":
            r["success"] = "响应回显=" + a["response"]
        elif a["type"] == "loopUntilResponse":
            r["wait"] = f"每{a['intervalMs']}ms；截止{a['timeoutMs']}ms"
            r["success"], r["failure"] = a["expectedResponse"], a["errorCode"]
        elif a["type"] == "readAndCompare":
            r["success"] = a["compareType"] + " " + str(a["compareValue"])
            r["failure"] = a["errorCode"]
            if r["failure"] == "FLOW_NOT_REACHED":
                r["action"] = "检查流量达到配方容量（旧name仍写500ml；实际compareValue如下）"
            r["target"] += "；解码=" + compact({k: a[k] for k in ("valueStartByte", "valueLength", "valueEndian", "valueType")})
        elif a["type"] == "readAndCache":
            r["action"] = "缓存" + a["cacheKey"]
            r["target"] += "；解码=" + compact({k: a[k] for k in ("cacheStartByte", "cacheLength", "cacheEndian", "valueType")})
        elif a["type"] == "timer":
            r["wait"] = a["seconds"] * 1000
            r["action"] = "显式等待(ms)"
        elif a["type"] == "updateUI":
            r["target"], r["action"] = a["uiTarget"], "更新UI=" + str(a["uiValue"])
        elif a["type"] == "setVariable":
            r["target"], r["action"] = a["key"], "设置变量=" + str(a["value"])
    elif kind == "move":
        index = args[0]
        p = points[index]
        fields = [key for key, value in recipe.items() if key.endswith(("_entry", "_work")) and value == index]
        r.update(point=index, target="/".join(fields), device="X / Y1 / Y2 / Z",
                 action=f"移动至 {p['name']}；PR=({p['x']},{p['y']},{p['y']},{p['z']})；仅变化轴触发",
                 wait="四电机各20ms轮询/4000ms截止", success="全部就绪；缓存四位置；UI使用X/Y1/Z", failure="MOTION_TIMEOUT",
                 production_point=copy.deepcopy(p), recipe_point_fields=fields)
    elif kind == "trigger":
        r.update(device=args[0], target="FC06 0x6002="+str(16+args[1]), action="触发预设PR"+str(args[1]), success="写入响应回显")
    elif kind in ("ready", "present"):
        r.update(device=args[0], target="FC04 0x000"+("1" if kind=="present" else "0"), action="等待设备完成" if kind=="ready" else "确认夹持容器",
                 wait=f"每20ms；截止{args[1]}ms", success="04 02 00 01", failure=args[2])
    elif kind == "coil":
        r.update(device=args[0], target="FC05 0x0000", action="开启" if args[1] else "关闭", success="写入响应回显")
    elif kind in ("volume", "fill_time"):
        r.update(device=args[0], target="FC06 "+("0x0010" if kind=="volume" else "0x0011"), action=("设置容量mL=" if kind=="volume" else "设置灌装ms=")+str(args[1]), success="写入响应回显")
    elif kind == "repeat_legacy":
        a = args[1][0]
        r.update(device=a["device"], target=a["request"], action="连续重复检查"+str(args[0])+"次；无额外等待", success=a["compareType"]+" "+str(a["compareValue"]), failure=a["errorCode"])
    elif kind == "read_volume":
        r.update(device=args[0], target="FC03 0x0012", action="读实灌量dispensed_ml并刷新dispensedMl")
    elif kind == "refresh":
        r.update(device="X / Y1 / Y2 / Z", target="FC03 0x602C, 2 registers", action="缓存四电机坐标；更新centerX/centerY/centerZ（centerY=Y1）")
    elif kind == "count":
        r.update(target="completed", action="完成计数加1并刷新UI")
    else:
        raise ValueError("Undocumented operation: " + kind)
    r["source"] = f"candidate:layered/L3_group/step_{stage:02d}_{STAGES[stage-1]}.group.json"
    return r


def build_payloads():
    """Return deterministic JSON/CSV content without changing repository files."""
    provenance = frozen_sources()
    context = replay.read(replay.INPUT/"context.json")
    raw = replay.read(replay.INPUT/"raw_response.json")
    source_files = set(replay.BASELINE.rglob("*.json"))
    source_files.update(replay.INPUT/name for name in (
        "prompt.txt", "context.json", "generator_instruction.txt", "raw_response.json", "provenance.json", "validation_report.json"))
    source_files.update(replay.REFERENCE/(flavor+"_small.json") for flavor in ("orange", "apple", "grape"))
    source_files.update(ROOT/name for name in (
        "scripts/build_beverage_full.py", "scripts/build_beverage_suite.py", "scripts/rebuild_beverage.py",
        "scripts/simulate_beverage_full.py", "scripts/simulate_beverage_suite.py", "scripts/verify_beverage_nl_extension.py",
        "examples/beverage_legacy/beverage_filling_legacy.json", "scripts/build_beverage_process_spec.py"))
    source_manifest = {"normalization": "CRLF to LF only", "files": {
        p.relative_to(ROOT).as_posix(): digest(p) for p in sorted(source_files)}}
    with tempfile.TemporaryDirectory(prefix="beverage-process-spec-") as temp:
        candidate = Path(temp)/"candidate"
        shutil.copytree(replay.BASELINE, candidate)
        changed = replay.apply_changes(candidate, raw)
        new_recipes = replay.check_final(candidate, context, replay.manifest(replay.BASELINE))
        documents = {p.relative_to(candidate).as_posix(): replay.read(p) for p in sorted(candidate.rglob("*.json"))
                     if p.relative_to(candidate).parts[0] != "legacy"}
        changes, _ = replay.differences(candidate)
    variables = documents["system_variables.json"]
    recipes, points = variables["recipes"], variables["production_points"]
    stage_rows = copy.deepcopy(STAGE_SUMMARIES)
    for row in stage_rows:
        row["source"] = "candidate:layered/L3_group/step_"+f"{row['stage']:02d}_"+row["stage_key"]+".group.json"
    old = old_steps(ROOT/"examples/beverage_legacy/beverage_filling_legacy.json")
    operations = [operation_record(r, points, stage, i, op) for r in new_recipes
                  for stage, plan in stage_plans(r, old).items() for i, op in enumerate(plan, 1)]
    definitions = documents["layered/L1_action/all_actions.json"]["actions"]
    nodes = documents["layered/L2_node/all_nodes.json"]["nodes"]
    groups = {k: v for k, v in documents.items() if k.startswith("layered/L3_group/")}
    # Confirm the documented plans correspond to the same frozen shared process,
    # not to a substituted example5 configuration.
    for key in groups:
        if key != replay.GROUP:
            if documents[key] != replay.read(replay.BASELINE/key):
                raise ValueError("Frozen shared group changed: " + key)
    before = replay.read(replay.BASELINE/"system_variables.json")
    legacy_before = sum(lines(replay.read(p)) for p in (replay.BASELINE/"legacy").glob("*.json"))
    legacy_added = sum(lines(replay.read(replay.REFERENCE/(f+"_small.json"))) for f in ("orange", "apple", "grape"))
    device_lines = lines(documents["device_registry.json"])
    layered_before = sum(lines(replay.read(p)) for p in replay.BASELINE.rglob("*.json") if p.relative_to(replay.BASELINE).parts[0] != "legacy")
    layered_after = sum(lines(d) for d in documents.values())
    if (layered_before, layered_after) != (changes["baseline_config_lines"], changes["candidate_config_lines"]):
        raise ValueError("Canonical line counting does not match archived four-space format")
    counts = {"monolithic_before":legacy_before+device_lines, "monolithic_after":legacy_before+legacy_added+device_lines,
              "monolithic_delta":legacy_added, "layered_before":layered_before, "layered_after":layered_after,
              "layered_delta":layered_after-layered_before}
    disclosure = {
        "candidate_derivation":"Unchanged example4 + recorded raw_response.json, using the archived apply_changes/check_final functions; example5 is not the generated candidate.",
        "original_input_scope":"Original prompt/context include engineering point/recipe data and a frozen dependency contract, not this later Excel workbook or every shared L3 body.",
        "supplement_scope":"Retrospective complete engineering documentation of the inherited process and its actual recorded extension; no new generation or success-rate measurement.",
        "model_provenance":provenance["generation_platform"], "raw_response_sha256":digest(replay.INPUT/"raw_response.json"),
        "references":"Three example5 monolithic files are pre-existing frozen evaluator references used only for the comparison/replay, not model input or claimed model output.",
        "loops":"L3 run_recipe has literal count=2 and produce_batch literal count=6. Recipe metadata matches those values but does not control the loops.",
        "coordinate_scope":"Synthetic precommissioned motor PR numbers, not physical Cartesian coordinates. No hardware is contacted.",
        "xlsx":"process_spec.xlsx", "xlsx_header_row":5, "xlsx_first_data_row":6}
    spec = dict(schema="beverage_complete_process_spec_v1", provenance=disclosure, stages=stage_rows,
                operations_250ml=operations, recipes=recipes, production_points=points,
                device_roles=DEVICE_ROLES, movement_contract=MOVEMENT_CONTRACT,
                tested_faults=FAULTS, fault_scope={'recipe_index':0,'recipe':'orange_family','volume_ml':2000,'count':11,'note':'One fault scenario per listed fault on the first existing 2L recipe; not eleven faults on each new 250mL recipe.'}, limitations=LIMITATIONS, counts=counts, changes=changes,
                configuration_documents=documents,
                document_origins={name: ("recorded raw_response changes applied to example4" if name in changed else "unchanged example4") for name in documents},
                recorded_inputs={name:(replay.read(replay.INPUT/name) if name.endswith(".json") else (replay.INPUT/name).read_text(encoding="utf-8-sig"))
                                 for name in ("prompt.txt","context.json","generator_instruction.txt","raw_response.json")})
    sheets = []
    sheets.append(sheet("工艺流程", "250 mL扩展所继承的完整十二阶段工艺", "01/11/12每配方一次，02–10每瓶一次；两批×六瓶是现有L3固定循环。成功条件与错误处置属于模拟模型。",
        ["阶段","工序","执行频次","点位路线字段","按顺序执行的动作","成功/互锁条件","等待/轮询","失败代码","范围与说明","定义来源"],
        [[s[k] for k in ("stage","name_zh","frequency","point_route_fields","actions_zh","success_zh","waits_zh","failure_codes_zh","notes_zh","source")] for s in stage_rows],
        [55,145,185,240,610,460,340,340,410,350]))
    sheets.append(sheet("250mL动作", "三个新增配方的完整一次工序动作清单", "每配方列一次初始化、每瓶阶段02–10、一次CIP和待机；逐瓶动作由L3重复12次，不复制铺开。每次move内部通用逻辑见通用子过程。",
        ["配方","阶段","步序","类型","执行频次","设备","生产点位ID","目标/协议/字段","动作","等待ms/规则","成功条件","失败代码","来源"],
        [[s[k] for k in ("recipe","stage","step","kind","frequency","device","point","target","action","wait","success","failure","source")] for s in operations],
        [135,55,55,135,170,180,85,370,470,250,330,240,380]))
    sheets.append(sheet("配方参数", "六个保留配方与三个250 mL新增配方", "灌装时间=向上取整(1000×容量/流量)；总数量和总容量可由公式复核。批数/每批数量元数据对应固定L3循环，并非动态控制循环。",
        ["配方","口味","规格","容量mL","流量mL/s","灌装时间ms","批数","每批数量","总数量","总容量mL","封盖模式","入料PR","封盖PR","出料PR","来源"],
        [[r['id'],r['flavor'],r['format'],r['volume_ml'],r['fill_rate_ml_s'],r['fill_time_ms'],r['batches'],r['units_per_batch'],r['batches']*r['units_per_batch'],r['volume_ml']*r['batches']*r['units_per_batch'],r['closure_mode'],r['infeed_pr'],r['closure_pr'],r['outfeed_pr'],"example4保留" if i<6 else "raw_response追加"] for i,r in enumerate(recipes)],
        [160,105,100,95,110,125,70,95,90,120,150,90,90,90,200]))
    fields = ['empty_entry','empty_work','rinse_entry','rinse_work','fill_entry','fill_work','closure_entry','closure_work','code_entry','code_work','finished_entry','finished_work']
    route_rows = []
    for r in recipes:
        for field in fields:
            p = points[r[field]]
            route_rows.append([r['id'],field,r[field],p['name'],p['x'],p['y'],p['y'],p['z'],r['fill_device'] if field.startswith('fill_') else '共享工位','candidate:system_variables.json'])
    sheets.append(sheet("配方路线", "每个配方引用的工位入口与作业点", "本表是配方字段映射；实际先后、抬升和原地复检见工艺流程与250mL动作。工位点是数组索引，PR点是各电机0–15编号。",
        ["配方","路线字段","生产点位ID","生产点位名","X PR","Y1 PR","Y2 PR","Z PR","工位选择","来源"], route_rows,
        [150,160,100,310,75,75,75,75,160,330]))
    point_rows=[]
    for i,p in enumerate(points):
        refs=[r['id']+':'+k for r in recipes for k in ['home_point']+fields if r[k]==i]
        point_rows.append([i,p['name'],p['x'],p['y'],p['y'],p['z'],'新增250mL' if i>=len(before['production_points']) else '保留基线','; '.join(refs),'candidate:system_variables.json'])
    sheets.append(sheet("生产点位", "49个生产点位与四定位电机PR映射", "前33个点保留，新增16个点；所有数值是预先调试好的PR编号，不是mm坐标。Y1/Y2分别发命令并轮询，不表示硬件同步。",
        ["点位ID","点位名","X PR","Y1 PR","Y2 PR","Z PR","来源类型","被哪些配方字段使用","来源"],point_rows,[80,320,75,75,75,75,150,700,320]))
    devices=documents['device_registry.json']['devices']
    sheets.append(sheet("设备接口", "21个设备的独立端口和站地址", "L1仅含Modbus PDU，不含站地址/CRC。设备标识由L2调用device解析至注册表；SIM_*均为虚拟端口，非可直接生产的端口配置。",
        ["设备ID","作用","端口","站地址","协议","来源"],
        [[d,DEVICE_ROLES[d],v['port'],v['address'],'Modbus PDU；传输层提供地址/CRC','candidate:device_registry.json'] for d,v in devices.items()],
        [160,590,150,90,350,320]))
    action_rows=[]
    for a in definitions:
        wrappers=[n for n in nodes if n['action']['template'].endswith('/'+a['filename'])]
        action_rows.append([a['filename'],a['type'],a.get('args',[]),a.get('request',''),a.get('parse',a.get('expression','')),a.get('response',''),'; '.join(n['filename'] for n in wrappers),'; '.join(compact(n.get('judge',{})) for n in wrappers),'; '.join(str(n.get('timeout_ms','')) for n in wrappers),'candidate:layered/L1_action/all_actions.json + L2_node/all_nodes.json'])
    sheets.append(sheet("操作接口", "56个L1动作及其62个L2状态包装", "字段与源码保持一致；L2调用节点的device指定设备。完整on_failure/on_timeout等定义保存在process_spec.json的configuration_documents，不由表格省略字段推断。",
        ["L1文件名","动作类型","参数定义","请求PDU","解码/计算","预期回显","使用的L2节点","L2判断","L2超时ms","来源"],action_rows,[450,200,370,330,430,330,560,360,220,470]))
    group_rows=[]
    for path,g in groups.items():
        brief=next((s['actions_zh'] for s in stage_rows if path.endswith(f"step_{s['stage']:02d}_{s['stage_key']}.group.json")), '')
        if not brief:
            brief={'run_recipe.group.json':'双参数选择配方；初始化；固定两批；CIP；待机',
                   'produce_batch.group.json':'固定六次produce_one_container', 'produce_one_container.group.json':'按顺序复用02–10九个工序',
                   'move_production_point.group.json':'生产点→XYZ PR；只触发变化轴；四电机就绪；刷新坐标',
                   'move_axis_if_changed.group.json':'读last轴PR；变化才按axis_devices依次触发；缓存新PR',
                   'trigger_motor.group.json':'PR 0–15编码为16+PR；写指定device的0x6002',
                   'refresh_position.group.json':'读X/Y1/Y2/Z 0x602C；界面Y采用Y1',
                   'poll_sensor_until_ready.group.json':'do-while先读再判断；未就绪才等待；截止/迭代保护'}.get(Path(path).name,'')
        group_rows.append([Path(path).name,g.get('args',[]),g.get('mode','sequence'),g.get('loop',''),brief,'candidate:'+path])
    sheets.append(sheet("通用子过程", "20个L3共享过程的接口与组合规则", "L4只传[口味,规格]；支持的新增规格上限由1改2。完整组体已随机器JSON发布。",["L3文件","参数契约","模式","循环条件","作用/顺序","来源"],group_rows,[390,510,110,420,670,470]))
    condition_rows = [[str(s['stage']).zfill(2),s['name_zh'],s['success_zh'],s['waits_zh'],s['failure_codes_zh'],'失败记录error并结束模拟场景；不表示物理安全停机或自动恢复',s['source']] for s in stage_rows]
    condition_rows += [['故障注入',name,'返回预期错误码','模型确定性注入',code,'单体与四层故障轨迹逐条比较','scripts/simulate_beverage_full.py:FAULTS'] for name,code in FAULTS.items()]
    condition_rows += [['范围','包装','未实现装箱/封箱/码垛及包装设备/点位','','','阶段10只包含成品放置和出料','完整配置与模拟器审计'],['范围','物理坐标','PR来自给定模拟调试数据','','','未建模物理碰撞、急停、Y轴同步或标定','scripts/simulate_beverage_suite.py'],['范围','输入来源','Excel为事后完整工艺说明','','','未替换或改写历史输入；不声称历史模型实际读取Excel','examples/beverage_nl_extension/provenance.json']]
    sheets.append(sheet("条件与异常", "工序成功条件、11项已验证故障及边界", "11项故障均在原有orange_family（2L）配方执行，不是每个250mL配方各11项。工序错误码列表更广，并非每个错误码都有独立注入测试。",["类别/阶段","检查对象","成功/预期结果","时间要求","错误码","处置/边界","来源"],condition_rows,[120,185,550,350,420,490,460]))
    comparison_rows=[]
    for label,b,l,info in [
        ('六配方完整配置',counts['monolithic_before'],layered_before,'单体=六份完整JSON+设备表；四层=全部L1/L2/L3/L4+设备表+点位/配方/变量表'),
        ('九配方完整配置',counts['monolithic_after'],layered_after,'单体追加三份冻结参考JSON；四层candidate由原始模型响应应用得到'),
        ('新增250mL三口味净增',legacy_added,layered_after-layered_before,'四层新L4共45行+系统变量净增183行；L3上限1→2为等长替换'),
        ('每个新增生产入口',legacy_added//3,15,'单体每份5134行；单个L4为15行，仅此行不含共享表，完整增量见上一行')]:
        comparison_rows.append([label,b,l,1-l/b,info])
    sheets.append(sheet("增量对比", "统一四空格JSON格式下的维护规模", "不展开批次循环；完整方案计入共享配置和辅助表。减少比例=1−四层/单体；净增行数不是人类工时。Excel/说明文档不是运行配置，未加入配置行数。",["对象","原始JSON","四层配置","减少比例","说明"],comparison_rows,[250,140,140,140,850]))
    workbook=dict(sheets=sheets,provenance=disclosure)
    baseline_documents={p.relative_to(replay.BASELINE).as_posix():replay.read(p)
                        for p in sorted(replay.BASELINE.rglob('*.json'))
                        if p.relative_to(replay.BASELINE).parts[0]!='legacy'}
    input_stages=copy.deepcopy(stage_rows)
    for stage in input_stages:
        stage['source']=stage['source'].replace('candidate:', 'baseline:')
    generation_input={
        'schema':'beverage_fuller_generation_input_v1',
        'scope':'Retrospective fuller-input package for a FUTURE separately recorded generation. NOT the historical model input, NOT a new experiment, and contains no generated candidate or raw response.',
        'request':(replay.INPUT/'prompt.txt').read_text(encoding='utf-8-sig'),
        'flavors':context['flavors'], 'size_indices':context['size_indices'],
        'new_size_engineering_data':context['new_size_engineering_data'],
        'baseline_documents':baseline_documents,
        'inherited_process_stages':input_stages,
        'movement_contract':{k:v for k,v in MOVEMENT_CONTRACT.items() if k not in ('point_count_after',)},
        'frozen_dependencies':context['frozen_dependencies'],
        'allowed_changes':['append new production points and recipes', 'extend recipe_lookup',
                           'replace run_recipe.args[1].max from 1 to 2', 'create three L4 entrances'],
        'limits':['No physical coordinate inference from volume.', 'No packaging/boxing station in the inherited flow.',
                  'Two batches and six bottles are fixed L3 loops; matching recipe metadata does not drive them.',
                  'Units such as MPa and N m are labels in original action names, not independently calibrated measurements.'],
        'sources':['examples/beverage_full/example4', 'examples/beverage_nl_extension/prompt.txt',
                   'examples/beverage_nl_extension/context.json', 'scripts/build_beverage_full.py',
                   'scripts/simulate_beverage_full.py']}
    original_vars=generation_input['baseline_documents']['system_variables.json']
    if len(original_vars['recipes'])!=6 or len(original_vars['production_points'])!=33:
        raise ValueError('Fuller input must contain only the original six recipes and 33 points')
    if any('small' in path for path in baseline_documents if path.startswith('layered/L4_flow/')):
        raise ValueError('Fuller input unexpectedly includes a 250mL output entrance')
    payloads={'process_spec.json':spec,'generation_input.json':generation_input,
              'source_manifest.json':source_manifest,'workbook_data.json':workbook}
    csvs={}
    for s in sheets:
        stream=io.StringIO(newline='')
        writer=csv.writer(stream,lineterminator='\n')
        writer.writerow(s['headers']); writer.writerows(s['rows'])
        csvs['tables/'+s['name']+'.csv']=stream.getvalue()
    # Normalize tuples used by the legacy generator to their JSON array representation.
    return json.loads(json.dumps(payloads,ensure_ascii=False)),csvs


def write_outputs(destination=DEST):
    payloads,csvs=build_payloads()
    destination=Path(destination); destination.mkdir(parents=True,exist_ok=True)
    (destination/'tables').mkdir(exist_ok=True)
    for name,data in payloads.items():
        (destination/name).write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')
    for name,data in csvs.items():
        (destination/name).write_text(data,encoding='utf-8-sig',newline='\n')
    return {'status':'PASS','output':str(destination),'sheets':{s['name']:len(s['rows']) for s in payloads['workbook_data.json']['sheets']},'configuration_documents':len(payloads['process_spec.json']['configuration_documents'])}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=DEST)
    args=parser.parse_args()
    print(json.dumps(write_outputs(args.output),ensure_ascii=False,indent=2))
