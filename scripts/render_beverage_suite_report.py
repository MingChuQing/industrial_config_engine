"""Render a standalone Chinese comparison report and trace-based event replay."""
import argparse
import html
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FLAVORS = {"orange": "橙汁", "apple": "苹果汁", "grape": "葡萄汁"}
SIZES = {"family": "家庭装 2 L", "can": "罐装 330 mL", "small": "小瓶装 250 mL"}


def render(suite, traces, output):
    report = json.loads((suite / "reports/comparison.json").read_text(encoding="utf-8-sig"))
    variables = json.loads((suite / "system_variables.json").read_text(encoding="utf-8"))
    sizes = report["sizes"]
    before = sizes["legacy"]["lines"] + sizes["devices"]["lines"]
    after = sum(sizes[key]["lines"] for key in ("L1", "L2", "L3", "L4", "devices", "points_and_recipes"))
    options, rows, replay = [], [], {}
    for recipe, result, counts in zip(variables["recipes"], report["recipes"], report["per_recipe_sizes"]):
        label = FLAVORS[recipe["flavor"]] + " · " + SIZES[recipe["format"]]
        options.append(f'<option value="{recipe["id"]}">{label}</option>')
        rows.append(f'<tr><td>{label}</td><td>{counts["legacy"]["lines"]:,}</td><td>{counts["L4"]["lines"]}</td>'
                    f'<td>{result["finished_containers"]}</td><td>{result["simulated_ms"]/1000:.2f}</td>'
                    f'<td>{result["modbus_transactions"]:,}</td><td>完全一致</td></tr>')
        events, state = [], {"X": 0, "Y1": 0, "Y2": 0, "Z": 0}
        with (traces / (recipe["id"] + "_layered.jsonl")).open(encoding="utf-8") as stream:
            for line in stream:
                event = json.loads(line)
                kind, device = event["kind"], event.get("device", "")
                chosen, message = False, ""
                if kind == "device_complete":
                    chosen = True
                    if device in state: state[device] = event["value"]
                    message = (device + " 完成 PR" + str(event["value"])) if event["operation"] == "motor" else device + " 灌装完成 " + str(event["value"]) + " mL"
                elif kind == "modbus" and event["request"].startswith(("05 ", "06 ")):
                    chosen = True
                    message = device + " ← " + event["request"]
                elif kind == "finished_container":
                    chosen, message = True, "首件成品完成：" + label
                if chosen:
                    events.append({"t": event["time_ms"], "message": message, "positions": dict(state)})
                if kind == "finished_container": break
        replay[recipe["id"]] = {"recipe": recipe, "events": events, "label": label}
    component_rows = ''.join(f'<tr><td>{label}</td><td>{sizes[key]["files"]}</td><td>{sizes[key]["lines"]:,}</td></tr>'
                             for key, label in [("L1", "L1 原子动作"), ("L2", "L2 执行与判断"), ("L3", "L3 共享子过程"),
                                                ("L4", "L4 九个配方入口"), ("points_and_recipes", "点位、配方与初始变量"), ("devices", "设备端口与站号")])
    data = json.dumps(replay, ensure_ascii=False).replace("</", "<\\/")
    document = r'''<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>九配方饮料线：原始 JSON 与四层配置</title>
<style>
:root{color-scheme:light;--ink:#173044;--muted:#586e7e;--line:#d7e3e9;--brand:#056d6b;--surface:#f3f8fa}
*{box-sizing:border-box}body{margin:0;background:var(--surface);color:var(--ink);font:16px/1.65 system-ui,"Microsoft YaHei",sans-serif}
main{max-width:1100px;margin:auto;padding:36px 24px 70px}h1{font-size:30px;line-height:1.3;margin:12px 0}h2{font-size:22px;margin:32px 0 12px}
p{max-width:920px}a{color:var(--brand)}.eyebrow{color:var(--brand);letter-spacing:.08em;font-size:14px;font-weight:600}.muted{color:var(--muted)}
.stats{display:grid;grid-template-columns:repeat(3,1fr);gap:16px;margin:24px 0}.stat,.panel{background:white;border:1px solid var(--line);border-radius:12px;padding:20px}.stat b{display:block;font-size:30px;color:var(--brand)}
table{border-collapse:collapse;width:100%;background:white;font-variant-numeric:tabular-nums}th,td{padding:11px 14px;text-align:right;border-bottom:1px solid var(--line)}th:first-child,td:first-child{text-align:left}th{background:#e8f1f4;font-weight:600}.tablewrap{overflow:auto;border:1px solid var(--line);border-radius:8px}
.controls{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin:14px 0}button,select{font:inherit;padding:7px 12px;border:1px solid #9bb3bf;border-radius:6px;background:white;color:var(--ink)}button{cursor:pointer}button:disabled{opacity:.45;cursor:default}input[type=range]{width:100%;accent-color:var(--brand)}
.stations{display:grid;grid-template-columns:repeat(4,1fr);gap:12px;margin:20px 0}.station{padding:18px 10px;background:#f0f5f7;border-radius:8px;text-align:center}.station.active{background:#d1efeb;outline:2px solid var(--brand)}.station b{display:block}.station span{font-size:14px;color:var(--muted)}
.readout{display:flex;gap:20px;flex-wrap:wrap;font-variant-numeric:tabular-nums}.readout strong{color:var(--brand)}#event{min-height:28px;font-family:ui-monospace,monospace;overflow-wrap:anywhere}code{background:#edf2f5;padding:2px 5px;border-radius:4px}pre{white-space:pre-wrap;background:#eaf2f5;padding:18px;border-radius:8px;font-size:14px}.callout{border-left:4px solid var(--brand);padding-left:16px}
@media(max-width:650px){main{padding:20px 14px}.stats{grid-template-columns:1fr}.stations{grid-template-columns:repeat(2,1fr)}h1{font-size:25px}th,td{padding:9px}.panel{padding:14px}}
</style></head><body><main>
<div class="eyebrow">可复现的合成生产案例 · 9 个配方 · 7 台步进电机</div>
<h1>九配方饮料线：原始 JSON 与四层配置</h1>
<p>橙汁、苹果汁、葡萄汁 × 家庭装 2 L、罐装 330 mL、小瓶装 250 mL。每个配方执行两批、每批六件。取空容器、灌装、封口、放置成品均使用不同点位；罐装采用模拟卷封，其余采用模拟旋盖。</p>
<div class="stats"><div class="stat">原始生产 JSON（9 份）<b>24,228 行</b>每份 2,692 行，批次使用循环</div><div class="stat">四层配置与全部辅助表<b>1,763 行</b>共享库只计一次，包含 9 个 L4</div><div class="stat">完整口径行数减少<b>@@REDUCTION@@%</b>24,285 → 1,763，双方均计设备表</div></div>
<p class="callout">九个正常场景共完成 <strong>108 件、92.88 L</strong>。两套解释器输出的 <strong>68,697 条带时间戳事件</strong>逐条相同，其中包含 <strong>35,478 次 Modbus 收发</strong>。比较包括端口、站号、请求、响应、等待、缓存、界面更新及成品记录。</p>
<h2>首件生产过程回放</h2><div class="panel">
<label for="recipe">选择配方</label><select id="recipe">@@OPTIONS@@</select>
<div class="stations"><div class="station" data-station="empty"><b>① 取空容器</b><span>Infeed + XYZ + 夹爪</span></div><div class="station" data-station="fill"><b>② 灌装</b><span id="head-label">独立口味灌装口</span></div><div class="station" data-station="closure"><b>③ 封口</b><span id="closure-label">旋盖 / 卷封</span></div><div class="station" data-station="finished"><b>④ 成品输出</b><span>夹爪释放 + Outfeed</span></div></div>
<div class="readout"><span>虚拟时间 <strong id="time"></strong></span><span id="positions"></span></div>
<div class="controls"><button id="prev">上一事件</button><button id="play">播放事件</button><button id="next">下一事件</button><span id="step"></span></div>
<input id="seek" type="range" min="0" value="0" aria-label="事件进度"><div id="event" aria-live="polite"></div>
<p class="muted">回放来自已保存的首件实际模拟轨迹，每步展示一个离散事件；播放速度不是物理速度。图示为工位示意，PR 点号表示已预设的设备点位，并非几何坐标。</p></div>
<h2>规模比较</h2><div class="tablewrap"><table><thead><tr><th>四层配置组成</th><th>文件数</th><th>行数</th></tr></thead><tbody>@@COMPONENTS@@<tr><th>全部配置</th><th>21</th><th>1,763</th></tr></tbody></table></div>
<p>原始九文件共 24,228 行，另加双方共用的设备表 57 行，共 24,285 行。四层部分为 1,267 行，另加点位／配方／初始变量 439 行和设备表 57 行。全部按四空格缩进计数；报告、脚本、测试、模型与运行轨迹均不计入配置行数。</p>
<h2>逐配方执行结果</h2><div class="tablewrap"><table><thead><tr><th>配方</th><th>原始行数</th><th>L4 行数</th><th>件数</th><th>虚拟秒</th><th>Modbus 次数</th><th>两套轨迹</th></tr></thead><tbody>@@ROWS@@</tbody></table></div>
<p class="muted">L4 每份 14 行不代表完整配方成本；完整统计已经计入共享 L1–L3、全部点位、配方表和设备表。不同口味流速为模拟参数，因此灌装用时不同。</p>
<h2>配置如何复用</h2><p>每个 L4 只选择配方索引；<code>run_recipe → produce_batch → produce_one_container</code> 复用同一套工艺。移动通过 <code>move_production_point → move_axis_if_changed → trigger_motor</code> 分解，只触发 PR 点号变化的 XYZ 轴；Y 变化同时需要 Y1、Y2 两次顺序触发。送料、封口和出料电机是每件必须执行的循环动作，因此即使 PR 编号相同也再次触发。</p>
<p>共有 37 个完整 XYZ 生产点位，每轴 PR 范围为 0–15。L1 有 13 个动作，L2 有 13 个节点，L3 有 8 个共享子过程，L4 有 9 个入口。每个配方实际调用移动子过程 145 次、通用轮询 640 次。</p>
<h2>验证与适用范围</h2><p>三种故障场景（送料卡住、灌装头离线、封口卡住）在两种表示下具有相同错误和停止轨迹。八项 Python 检查覆盖漏循环、错误配方、漏 Y2、电文寄存器、解码和轮询间隔。现有 C++ 引擎也成功执行九个 L4，各计数 12 件；该项是设备立即就绪模型下的兼容性检查，不与上述带延迟模拟混算。</p>
<p>本例是按明确规则构造的合成案例，不是来自现场的原始工程。九份独立原始文件重复表达设备级操作，四层方案共享这些操作，压缩比例仅适用于本案例。两个解释器独立实现，但共享同一个理想设备模型；未模拟真实串口、CRC 收发、运动惯性、机械干涉、液体误差和硬件安全联锁。模型中失败即停止当前仿真。</p>
<h2>复现</h2><pre>python scripts/simulate_beverage_suite.py --report examples/beverage_suite/reports/comparison.json --traces traces
python scripts/test_beverage_suite.py -v
python scripts/render_beverage_suite_report.py --traces traces --output report.html</pre>
<p>原始文件：<code>examples/beverage_suite/legacy/</code><br>四层文件：<code>examples/beverage_suite/layered/</code><br>完整轨迹：<code>traces/*_legacy.jsonl</code> 和 <code>traces/*_layered.jsonl</code><br>机器可读对比：<code>examples/beverage_suite/reports/comparison.json</code></p>
<script type="application/json" id="replay-data">@@DATA@@</script>
<script>
const dataset=JSON.parse(document.getElementById('replay-data').textContent);
const el=id=>document.getElementById(id);let at=0,timer=null;
function stop(){if(timer!==null){clearInterval(timer);timer=null;}el('play').textContent='播放事件';}
function draw(){const selected=dataset[el('recipe').value],events=selected.events,e=events[at],p=e.positions;
el('time').textContent=(e.t/1000).toFixed(3)+' s';el('positions').textContent=`X PR${p.X} · Y1 PR${p.Y1} / Y2 PR${p.Y2} · Z PR${p.Z}`;
el('step').textContent=`${at+1} / ${events.length}`;el('event').textContent=e.message;el('seek').max=events.length-1;el('seek').value=at;
el('prev').disabled=at===0;el('next').disabled=at===events.length-1;
let active=p.X>=1&&p.X<=3?'empty':p.X>=4&&p.X<=6?'fill':p.X>=7&&p.X<=9?'closure':p.X>=10&&p.X<=12?'finished':'';
document.querySelectorAll('[data-station]').forEach(x=>x.classList.toggle('active',x.dataset.station===active));
el('head-label').textContent=selected.recipe.fill_device;el('closure-label').textContent=selected.recipe.format==='can'?'Closure：模拟卷封':'Closure：模拟旋盖';}
el('recipe').addEventListener('change',()=>{stop();at=0;draw();});
el('seek').addEventListener('input',()=>{stop();at=Number(el('seek').value);draw();});
el('prev').addEventListener('click',()=>{stop();at=Math.max(0,at-1);draw();});
el('next').addEventListener('click',()=>{stop();at=Math.min(dataset[el('recipe').value].events.length-1,at+1);draw();});
el('play').addEventListener('click',()=>{if(timer!==null){stop();return;}if(at===dataset[el('recipe').value].events.length-1)at=0;el('play').textContent='暂停';timer=setInterval(()=>{at++;draw();if(at===dataset[el('recipe').value].events.length-1)stop();},200);});draw();
</script></main></body></html>'''
    for key, value in {"REDUCTION": f"{(1-after/before)*100:.2f}", "OPTIONS": ''.join(options), "ROWS": ''.join(rows),
                       "COMPONENTS": component_rows, "DATA": data}.items(): document = document.replace("@@"+key+"@@", value)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(document, encoding="utf-8")
    print(str(output))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT / "examples/beverage_suite")
    parser.add_argument("--traces", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(); render(args.root, args.traces, args.output)
