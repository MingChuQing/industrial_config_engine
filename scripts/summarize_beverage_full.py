"""Compare complete size and the actual six-to-nine-recipe configuration edits."""
import argparse
import difflib
import json
from pathlib import Path
from simulate_beverage_suite import read, size

ROOT=Path(__file__).resolve().parents[1]


def summarize_data(root, reports=None):
    """Calculate the comparison without changing configuration or report files."""
    root=Path(root)
    reports=reports if reports is not None else {
        name:read(root/'reports'/(name+'_simulation.json')) for name in ('example4','example5')}
    stages=[]
    for name in ('example4','example5'):
        phase=root/name
        report=reports[name]
        assert report['status']=='PASS'
        amounts=report['sizes']
        raw=amounts['legacy']['lines']+amounts['devices']['lines']
        layered=sum(v['lines'] for k,v in amounts.items() if k!='legacy')
        covered={k:set().union(*(set(r[k]) for r in report['recipes'])) for k in ('used_actions','used_nodes','used_groups')}
        assert {a['filename'] for a in read(phase/'layered/L1_action/all_actions.json')['actions']}==covered['used_actions']
        assert {a['filename'] for a in read(phase/'layered/L2_node/all_nodes.json')['nodes']}==covered['used_nodes']
        assert {'L3_group/'+p.name for p in (phase/'layered/L3_group').glob('*.json')}==covered['used_groups']
        stages.append({'name':name,'recipes':len(report['recipes']),'sizes':amounts,'legacy_complete_lines':raw,
                       'layered_complete_lines':layered,'reduction_percent':round((1-layered/raw)*100,2),
                       'finished_containers':sum(r['finished_containers'] for r in report['recipes']),
                       'modbus_transactions':sum(r['modbus_transactions'] for r in report['recipes']),
                       'all_templates_used':True})
    before,after=root/'example4',root/'example5'
    changes,patches=[],[]
    for p in sorted(after.rglob('*.json')):
        relative=p.relative_to(after)
        prev=before/relative
        a=prev.read_text(encoding='utf-8').splitlines(True) if prev.exists() else []
        b=p.read_text(encoding='utf-8').splitlines(True)
        if a==b: continue
        plus=minus=0
        for tag,i,j,k,l in difflib.SequenceMatcher(a=a,b=b,autojunk=False).get_opcodes():
            if tag!='equal': plus+=l-k;minus+=j-i
        changes.append({'file':relative.as_posix(),'kind':'modified' if prev.exists() else 'new',
                        'added_lines':plus,'removed_lines':minus,'net_lines':len(b)-len(a)})
        patches.extend(difflib.unified_diff(a,b,fromfile='example4/'+relative.as_posix(),tofile='example5/'+relative.as_posix()))
    assert [c['file'] for c in changes if c['kind']=='modified']==['layered/L3_group/run_recipe.group.json','system_variables.json']
    r4=reports['example4']['recipes'];r5=reports['example5']['recipes']
    assert [(r['recipe'],r['trace_sha256']) for r in r4]==[(r['recipe'],r['trace_sha256']) for r in r5[:6]]
    result={'status':'PASS','scope':'Restored twelve-stage synthetic case; two-parameter L4; all shared definitions and data included.',
            'phases':stages,'extension':{'legacy_net_lines':stages[1]['legacy_complete_lines']-stages[0]['legacy_complete_lines'],
                 'layered_net_lines':stages[1]['layered_complete_lines']-stages[0]['layered_complete_lines'],
                 'old_six_traces_unchanged':True,'changes':changes}}
    return result,''.join(patches)


def summarize(root):
    root=Path(root)
    result,patch=summarize_data(root)
    (root/'comparison.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (root/'extension.diff').write_text(patch,encoding='utf-8')
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,default=ROOT/'examples/beverage_full')
    summarize(p.parse_args().root)
