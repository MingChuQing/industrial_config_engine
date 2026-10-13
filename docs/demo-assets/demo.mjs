const $ = id => document.getElementById(id);
const labels = {
 en: {brand:'Interactive code demo',paper:'Paper',title:'Run the original simulator',intro:'Compare a monolithic JSON process with the same process composed from L1–L4. Python executes in your browser, using the paper’s saved configurations and synthetic device model.',recipe:'Recipe',scenario:'Scenario',run:'Run demo',cancel:'Cancel',scope:'Synthetic simulation only. No physical devices are connected. The first run downloads about 18 MB of runtime and configurations; later runs reuse the loaded Python runtime.',faultNote:'Diagnostic faults use the representative orange 2 L recipe tested in the paper.',errorTitle:'Run did not complete',errorHelp:'You can try again. Any failed execution or disagreement with archived evidence is reported as an error.',result:'Fresh execution result',stages:'Twelve-stage process',stageNote:'Counts show how often each stage was entered. Stages 02–10 repeat once per container.',config:'Executed L4 entry',trace:'Highlighted execution events',traceNote:'The first 36 stage and finished-container events from the actual timed trace.',time:'Virtual time (ms)',event:'Event',detail:'Detail',benchmark:'Complete benchmark',dataset:'Dataset',rawLines:'Monolithic lines',layeredLines:'Four-layer lines',reduction:'Reduction',containers:'Containers',faults:'Fresh diagnostic-fault results',status:'Status',agreement:'Timed traces',raw:'Raw result and reproducibility',downloadReport:'Download result JSON',downloadTrace:'Download full trace',source:'Code and reproduction',sourceNote:'This page runs unchanged copies of the original Python interpreters. Fresh results are checked against the archived reports, including exact timed-trace hashes. The demo does not run the separate C++ configuration-mutation study.',simulator:'View simulator source ↗',data:'View configurations ↗',localCommand:'To reproduce the complete verification locally, run this command in the repository:',manifest:'Source manifest',footer:'Passing these simulations does not establish physical safety or independent engineering-review reliability.',loading:'Loading browser Python and saved configurations…',running:'Running both interpreters and comparing timed traces…',cancelled:'Execution cancelled. Click Run demo to start again.',success:'Production completed',detected:'Fault detected',pass:'Benchmark reproduced',match:'Match',filled:'Filled volume (mL)',virtual:'Virtual time (s)',transactions:'Modbus transactions',events:'Trace events',elapsed:'Python execution (s)',normalPairs:'Normal recipe pairs',faultPairs:'Diagnostic fault pairs',unchanged:'Original six recipes unchanged',yes:'Yes',visits:'visits',notReached:'Not reached',normal:'Normal production',cip_pressure_low:'CIP pressure low',level_fault:'Level recheck fails',low_flow:'Insufficient flow',benchmarkOption:'Full paper benchmark',orange:'Orange',apple:'Apple',grape:'Grape'},
 zh: {brand:'代码交互演示',paper:'论文',title:'一键运行原始模拟器',intro:'运行同一条饮料生产流程的单一 JSON 与 L1–L4 四层配置，比较两者的执行轨迹。浏览器实际执行 Python，使用论文存档的配置和虚拟设备模型。',recipe:'生产配方',scenario:'运行场景',run:'运行演示',cancel:'取消',scope:'仅运行虚拟设备模拟，不连接真实设备。首次运行需下载约 18 MB 的运行环境与配置；后续运行复用已加载的 Python 环境。',faultNote:'故障诊断使用论文测试的代表配方：橙味 2 L 家庭装。',errorTitle:'运行未完成',errorHelp:'可以重新尝试。执行失败或结果与存档证据不一致时，会直接报告错误。',result:'本次实际运行结果',stages:'十二阶段生产流程',stageNote:'次数表示进入该工序的次数。第 02–10 阶段每生产一瓶重复一次。',config:'实际执行的 L4 入口',trace:'执行轨迹摘录',traceNote:'从实际带时间戳轨迹中选取前 36 条工序与成品事件。',time:'虚拟时间（毫秒）',event:'事件',detail:'详情',benchmark:'完整基准对比',dataset:'数据集',rawLines:'单一 JSON 行数',layeredLines:'四层配置行数',reduction:'减少比例',containers:'成品数量',faults:'本次故障诊断结果',status:'状态',agreement:'带时间戳轨迹',raw:'原始结果与复现信息',downloadReport:'下载结果 JSON',downloadTrace:'下载完整轨迹',source:'源码与本地复现',sourceNote:'本页运行原始 Python 解释器的未修改副本，并将本次结果与存档报告逐项核对，包括完整带时间戳轨迹的哈希。本演示不运行独立的 C++ 配置缺陷实验。',simulator:'查看模拟器源码 ↗',data:'查看配置文件 ↗',localCommand:'在代码仓库中执行以下命令，可复现完整验证：',manifest:'源码清单',footer:'模拟通过不等同于真实设备的安全保证，也不证明独立工程审查的有效性。',loading:'正在加载浏览器 Python 和生产配置……',running:'正在运行两套解释器并对比带时间戳轨迹……',cancelled:'已取消运行。点击“运行演示”可重新开始。',success:'生产完成',detected:'已检测到故障',pass:'基准结果复现通过',match:'一致',filled:'总灌装量（mL）',virtual:'虚拟运行时间（秒）',transactions:'Modbus 事务',events:'轨迹事件',elapsed:'Python 执行耗时（秒）',normalPairs:'正常配方对比组',faultPairs:'故障对比组',unchanged:'原六种配方轨迹保持一致',yes:'是',visits:'次',notReached:'未进入',normal:'正常生产',cip_pressure_low:'CIP 压力不足',level_fault:'液位复检失败',low_flow:'流量不足',benchmarkOption:'完整论文基准',orange:'橙味',apple:'苹果味',grape:'葡萄味'}
};
const stages = [
 ['Initialization','系统初始化'],['Empty container feeding','空瓶送料'],['Position detection','瓶位检测'],['Rinsing','瓶内冲洗'],['Filling positioning','灌装定位'],['Filling','灌装'],['Level recheck','液位复检'],['Capping','封盖'],['Coding / labeling','打码贴标'],['Product discharge','成品出料'],['CIP cleaning','CIP 清洗'],['Homing / standby','回零待机']
];
let language = localStorage.getItem('demo-language') || (navigator.language.startsWith('zh')?'zh':'en');
let manifest, worker, busy=false, lastResult, requestId=0, progressKey='loading', selectedRecipe='orange_small';
const t = key => labels[language][key] || key;
const json = value => JSON.stringify(value,null,2);
const number = value => Number(value).toLocaleString(language==='zh'?'zh-CN':'en-US');
function node(tag,content,className) { const e=document.createElement(tag);e.textContent=content;if(className)e.className=className;return e; }
function recipeLabel(r) {return `${t(r.flavor)} · ${r.volume_ml===2000?'2 L':r.volume_ml+' mL'}`;}
function translate() {
 document.documentElement.lang=language==='zh'?'zh-CN':'en';
 document.querySelectorAll('[data-i18n]').forEach(e=>e.textContent=t(e.dataset.i18n));
 $('language').textContent=language==='zh'?'English':'中文';
 for(const o of $('scenario').options)o.textContent=t(o.value==='benchmark'?'benchmarkOption':o.value);
 if(manifest) for(const o of $('recipe').options)o.textContent=recipeLabel(manifest.recipes.find(r=>r.id===o.value));
 if(busy)$('progress-text').textContent=t(progressKey);
 if(lastResult)renderResult(lastResult);
}
function scenarioChanged() {
 const fault=!['normal','benchmark'].includes($('scenario').value);
 if(fault){$('recipe').value='orange_family';}else{$('recipe').value=selectedRecipe;}
 $('recipe').disabled=busy||$('scenario').value!=='normal';
 $('fault-note').hidden=!fault;
}
function setBusy(value){busy=value;$('run').disabled=value;$('scenario').disabled=value;$('cancel').hidden=!value;scenarioChanged();}
function showError(message){$('error-text').textContent=message;$('error').hidden=false;$('progress').hidden=true;setBusy(false);}
function metrics(entries){$('metrics').replaceChildren(...entries.map(([label,value])=>{const e=document.createElement('div');e.append(node('dt',t(label)),node('dd',value));return e;}));}
function row(values){const e=document.createElement('tr');values.forEach((v,i)=>e.append(node('td',String(v),i===2?'detail':undefined)));return e;}
function renderResult(r){
 $('results').hidden=false;
 const benchmark=r.mode==='benchmark';
 $('single-case').hidden=benchmark;$('benchmark-results').hidden=!benchmark;$('download-trace').hidden=benchmark;
 $('badge').textContent=t(benchmark?'pass':r.fault?'detected':'success');
 $('badge').className=r.fault?'fault':'success';
 $('result-explanation').textContent=language==='zh'?
  `两种配置的完整带时间戳轨迹一致，本次结果与论文存档逐项一致。${benchmark?'所有 15 组正常配方与 22 组故障对比均完成。':`状态：${r.status}；配方：${recipeLabel(r.recipe_data)}。`}`:
  `The complete timed traces agree, and fresh results match the archived evidence. ${benchmark?'All 15 normal recipe pairs and 22 diagnostic fault pairs completed.':`Status: ${r.status}. Recipe: ${recipeLabel(r.recipe_data)}.`}`;
 if(benchmark){
  metrics([['normalPairs',r.normal_pair_runs],['faultPairs',r.fault_pair_runs],['containers',r.summary.phases[1].finished_containers],['transactions',number(r.summary.phases[1].modbus_transactions)],['unchanged',r.summary.extension.old_six_traces_unchanged?t('yes'):'NO'],['elapsed',r.browser_python_seconds]]);
  $('benchmark-rows').replaceChildren(...r.summary.phases.map(p=>row([`${p.name} (${p.recipes})`,number(p.legacy_complete_lines),number(p.layered_complete_lines),p.reduction_percent+'%',p.finished_containers])));
  const x=r.summary.extension;
  $('extension-summary').textContent=language==='zh'?`新增三种口味 250 mL 配方：四层结构净增加 ${number(x.layered_net_lines)} 行，单一 JSON 净增加 ${number(x.legacy_net_lines)} 行；原六种配方的带时间戳轨迹保持一致。`:`Adding three 250 mL recipes: ${number(x.layered_net_lines)} net lines for L1–L4 versus ${number(x.legacy_net_lines)} for monolithic JSON. Timed traces of the original six recipes are unchanged.`;
  $('fault-rows').replaceChildren(...r.fault_cases.map(f=>row([t(f.fault),f.status,f.trace_equal?t('match'):'MISMATCH'])));
 }else{
  metrics([['containers',r.finished_containers],['filled',number(r.filled_total_ml)],['virtual',r.simulated_ms/1000],['transactions',number(r.modbus_transactions)],['events',number(r.events)],['elapsed',r.browser_python_seconds]]);
  $('stage-list').replaceChildren(...stages.map((s,i)=>{const key=String(i+1).padStart(2,'0');const count=Object.entries(r.stage_counts).find(([name])=>name.startsWith(key))?.[1]||0;const e=document.createElement('li');if(!count)e.className='unreached';e.append(node('span',key,'number'),node('strong',s[language==='zh'?1:0]),node('span',count?`${count} ${t('visits')}`:t('notReached'),'visits'));return e;}));
  $('configuration').textContent=json(r.L4);
  $('trace-rows').replaceChildren(...r.trace_sample.map(e=>{const{time_ms,kind,...detail}=e;return row([time_ms,kind,JSON.stringify(detail)]);}));
 }
 const {trace_jsonl,...report}=r;
 $('raw-result').textContent=json(report);
 const link=document.createElement('a');link.href=`https://github.com/MingChuQing/industrial_config_engine/tree/${r.source_commit}`;link.textContent=r.source_commit.slice(0,12);link.target='_blank';link.rel='noopener';
 $('source-version').replaceChildren(document.createTextNode(language==='zh'?'模拟器与配置的固定版本：':'Frozen simulator / configuration version: '),link);
}
function start(){
 $('error').hidden=true;$('results').hidden=true;lastResult=undefined;
 progressKey=worker?'running':'loading';$('progress-text').textContent=t(progressKey);$('progress').hidden=false;
 $('progress').querySelector('.spinner').hidden=false;setBusy(true);const id=++requestId;
 if(!worker){
  worker=new Worker(new URL('./demo-worker.mjs',import.meta.url),{type:'module'});
  worker.onmessage=({data})=>{if(data.id&&data.id!==requestId)return;if(data.type==='progress'){progressKey=data.stage;$('progress-text').textContent=t(progressKey);}else if(data.type==='error'){showError(data.error);worker.terminate();worker=undefined;}else if(data.type==='result'){lastResult=data.result;$('progress').hidden=true;setBusy(false);renderResult(lastResult);$('results').scrollIntoView({behavior:matchMedia('(prefers-reduced-motion: reduce)').matches?'instant':'smooth',block:'start'});}};
  worker.onerror=e=>{showError(e.message||'Browser Python failed to load');worker.terminate();worker=undefined;};
 }
 worker.postMessage({id,recipe:$('recipe').value,scenario:$('scenario').value});
}
function download(content,name,type){const a=document.createElement('a');const url=URL.createObjectURL(new Blob([content],{type}));a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),10000);}
$('run').addEventListener('click',start);
$('cancel').addEventListener('click',()=>{requestId++;worker?.terminate();worker=undefined;setBusy(false);$('progress-text').textContent=t('cancelled');$('progress').hidden=false;$('progress').querySelector('.spinner').hidden=true;});
$('language').addEventListener('click',()=>{language=language==='zh'?'en':'zh';localStorage.setItem('demo-language',language);translate();});
$('recipe').addEventListener('change',()=>{selectedRecipe=$('recipe').value;});
$('scenario').addEventListener('change',scenarioChanged);
$('download-report').addEventListener('click',()=>{if(lastResult){const{trace_jsonl,...report}=lastResult;download(json(report)+'\n','simulation-result.json','application/json');}});
$('download-trace').addEventListener('click',()=>{if(lastResult?.trace_jsonl)download(lastResult.trace_jsonl,`${lastResult.recipe}${lastResult.fault?'_'+lastResult.fault:''}.jsonl`,'application/x-ndjson');});
translate();
try{const response=await fetch(new URL('./manifest.json',import.meta.url));if(!response.ok)throw new Error(`Source manifest: HTTP ${response.status}`);manifest=await response.json();$('recipe').replaceChildren(...manifest.recipes.map(r=>{const o=node('option',recipeLabel(r));o.value=r.id;return o;}));$('recipe').value=selectedRecipe;translate();}catch(error){showError(error.message);}
