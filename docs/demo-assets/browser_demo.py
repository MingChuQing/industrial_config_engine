"""Browser-only entry point. Original simulator modules are bundled unchanged."""
from pathlib import Path
import json
import time
from simulate_beverage_full import run_pair, compare, FAULTS
from summarize_beverage_full import summarize_data

ROOT=Path('/engine/examples/beverage_full')

def run_demo(recipe_id='orange_small',scenario='normal'):
    start=time.perf_counter()
    if scenario=='benchmark':
        reports={name:compare(ROOT/name) for name in ('example4','example5')}
        summary,_=summarize_data(ROOT,reports)
        for name,report in reports.items():
            saved=json.loads((ROOT/'reports'/(name+'_simulation.json')).read_text())
            assert report==saved, 'Fresh browser result differs from the archived '+name+' evidence'
        saved_summary=json.loads((ROOT/'comparison.json').read_text())
        assert summary==saved_summary, 'Fresh size comparison differs from the saved benchmark'
        result={'mode':'benchmark','status':'PASS','summary':summary,
                'recipes':reports['example5']['recipes'],
                'fault_cases':reports['example5']['fault_cases'],
                'normal_pair_runs':15,'fault_pair_runs':22,'archived_results_match':True}
    else:
        phase=ROOT/'example5'
        variables=json.loads((phase/'system_variables.json').read_text())
        if scenario=='normal':
            index=next(i for i,r in enumerate(variables['recipes']) if r['id']==recipe_id)
            fault=None
        else:
            assert scenario in FAULTS, 'Unsupported diagnostic scenario'
            # The paper tests each diagnostic fault on the first representative recipe.
            index=0
            fault=scenario
        recipe=variables['recipes'][index]
        traces=Path('/tmp/demo-traces')
        result=run_pair(phase,index,fault,trace_dir=traces)
        saved=json.loads((ROOT/'reports/example5_simulation.json').read_text())
        controls=saved['fault_cases'] if fault else saved['recipes']
        archived=next(r for r in controls if (r['fault']==fault if fault else r['recipe']==recipe['id']))
        assert result==archived, 'Fresh browser result differs from the archived execution'
        suffix='_'+fault if fault else ''
        trace_file=traces/(recipe['id']+suffix+'_legacy.jsonl')
        trace=trace_file.read_text()
        events=[json.loads(line) for line in trace.splitlines()]
        highlights=[e for e in events if e['kind']=='finished_container' or (e['kind']=='ui' and e.get('target')=='stage')]
        result.update(mode='case',recipe_data=recipe,archived_results_match=True,
                      expected_status=FAULTS[fault] if fault else 'SUCCESS',
                      trace_sample=highlights[:36],trace_jsonl=trace,
                      L4=json.loads((phase/'layered/L4_flow'/(recipe['id']+'.json')).read_text()))
        for file in traces.glob('*.jsonl'):file.unlink()
    result['browser_python_seconds']=round(time.perf_counter()-start,3)
    result['runtime']='Original Python simulator executed in browser Python'
    return json.dumps(result,ensure_ascii=False,separators=(',',':'))
