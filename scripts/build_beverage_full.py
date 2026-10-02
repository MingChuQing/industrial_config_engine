"""Restore twelve process stages in both representations of the parameterized line."""
import argparse
import copy
import json
import math
from pathlib import Path
from build_beverage_suite import (LayeredSuite, FLAVORS, FORMATS, MOTORS, raw_write,
                                 raw_read, raw_poll, expand_raw)
from rebuild_beverage import save

ROOT = Path(__file__).resolve().parents[1]
STAGES = ['system_initialization', 'empty_bottle_feeding', 'bottle_position_detection',
          'in_bottle_rinsing', 'filling_positioning', 'filling', 'level_recheck',
          'capping', 'coding_labeling', 'finished_product_discharge',
          'cip_cleaning_sanitizing', 'homing_standby']


def flatten(item):
    if isinstance(item, list):
        return [a for x in item for a in flatten(x)]
    for key in ('steps', 'sequences', 'groups', 'actions'):
        if key in item:
            return flatten(item[key])
    return [copy.deepcopy(item)]


def old_steps(path):
    doc = json.loads(path.read_text(encoding='utf-8-sig'))
    return [flatten(s) for s in doc['steps']]


def without_old_moves(items):
    result, i = [], 0
    while i < len(items):
        x = items[i]
        if x.get('request', '').startswith('06 60 02') and x.get('device') in MOTORS[:4]:
            while i < len(items) and items[i].get('request', '').startswith('06 60 02'):
                i += 1
            assert items[i]['type'] == 'timer'
            i += 1
            assert [a['type'] for a in items[i:i+7]] == ['readAndCache'] * 4 + ['updateUI'] * 3
            i += 7
            continue
        x = copy.deepcopy(x)
        if x['type'] == 'readAndCache' and x.get('request', '').startswith('04'):
            # Read the entire 0001 status word, not its zero high byte.
            x.update(cacheStartByte=2, cacheLength=2, cacheEndian='big', valueType='uint16')
        result.append(x)
        i += 1
    return result


def specification(include_small):
    points, point_ids, recipes = [], {}, []
    def point(x, y, z, name):
        key = (x, y, z)
        if key not in point_ids:
            point_ids[key] = len(points)
            points.append({'name': name, 'x': x, 'y': y, 'z': z})
        return point_ids[key]
    home = point(0, 0, 0, 'Commissioned home PR0')
    order = [(fi, si) for fi in range(3) for si in range(2)]
    if include_small:
        order += [(fi, 2) for fi in range(3)]
    for fi, si in order:
        flavor, head, rate = FLAVORS[fi]
        size, volume, closure = FORMATS[si]
        r = {'id': flavor+'_'+size, 'flavor': flavor, 'format': size, 'volume_ml': volume,
             'fill_device': head, 'fill_rate_ml_s': rate, 'fill_time_ms': math.ceil(volume*1000/rate),
             'closure_mode': closure, 'closure_pr': si+1, 'infeed_pr': si+1, 'outfeed_pr': si+4,
             'home_point': home, 'batches': 2, 'units_per_batch': 6}
        for station, x, y in [('empty', si+1, si+1), ('rinse', 13, si+1),
                              ('fill', fi+4, si+10), ('closure', si+7, si+4),
                              ('code', 14, si+1), ('finished', si+10, si+7)]:
            for suffix, z in [('entry', 1), ('work', si+2)]:
                r[station+'_'+suffix] = point(x, y, z, f'{flavor} {size} {station} {suffix}')
        recipes.append(r)
    devices = {name: {'port': 'SIM_MOTION', 'address': i+1} for i, name in enumerate(MOTORS)}
    process = [head for _, head, _ in FLAVORS] + ['Gripper', 'CapFeeder', 'ContainerSensor',
               'RinseValve', 'CIPValve', 'Relay', 'BottleSensor', 'LevelSensor',
               'PressureSensor', 'FlowMeter', 'Torque']
    devices.update({name: {'port': 'SIM_PROCESS', 'address': i+1} for i, name in enumerate(process)})
    registry = {'configuration_note': 'Synthetic devices; old process checks restored, no physical I/O.', 'devices': devices}
    variables = {'production_points': points, 'recipes': recipes,
                 'recipe_lookup': [[next(i for i, r in enumerate(recipes) if r['flavor'] == FLAVORS[fi][0]
                                         and r['format'] == FORMATS[si][0])
                                    for si in range(3 if include_small else 2)] for fi in range(3)],
                 'last_x_pr': -1, 'last_y_pr': -1, 'last_z_pr': -1, 'completed': 0}
    return recipes, registry, variables


def marker(i):
    return {'name': 'Stage '+str(i), 'type': 'updateUI', 'uiTarget': 'stage', 'uiValue': f'{i:02d}'}


def stage_plans(r, old):
    def native(items): return [('legacy', a) for a in items]
    def select(stage, kind, device=None, request=None):
        return copy.deepcopy(next(a for a in old[stage-1] if a['type'] == kind and
            (device is None or a.get('device') == device) and (request is None or a.get('request') == request)))
    def move(station, suffix='work'): return ('move', r[station+'_'+suffix])
    reset = [raw_write(d, 0, 0, 5) for d in [h for _, h, _ in FLAVORS]+['Gripper','CapFeeder']]
    reset += [raw_write(d, address, 0, 5) for d, address in [('RinseValve',0x12),('CIPValve',0x13),
               ('CIPValve',0x14)] + [('Relay', a) for a in range(8,15)]]
    alarms = [a for a in old[0] if a['type'] == 'readAndCompare' and a.get('request') == '03 22 03 00 01']
    home = [('trigger', m, 0) for m in MOTORS[:4]]
    home += [('ready', m, 4000, 'HOME_TIMEOUT') for m in MOTORS[:4]]
    home += [('legacy', {'name':'Remember home', 'type':'setVariable', 'key':'last_'+a+'_pr', 'value':0}) for a in 'xyz']
    home += [('refresh',)]
    pressure = select(6, 'readAndCompare', 'PressureSensor')
    flow_check = select(6, 'readAndCompare', 'FlowMeter')
    flow_check['compareValue'] = r['volume_ml']
    flow_check['errorMessage'] = 'Dispensed volume below recipe target'
    flow_cache = select(6, 'readAndCache', 'FlowMeter')
    level = select(6, 'loopUntilResponse', 'LevelSensor')
    stages = {}
    stages[1] = native(reset) + home + native(alarms)
    stages[1] += native([a for a in old[0] if a['type'] == 'readAndCache' and a.get('request') == '03 10 03 00 01'])
    stages[1] += native([{'name':'Initialization settle','type':'timer','seconds':1}])
    feed_polls = [a for a in old[1] if a['type'] == 'loopUntilResponse']
    stages[2] = native([raw_write('Relay',0x08,1,5)])
    stages[2] += [('trigger','Infeed',r['infeed_pr']), ('ready','Infeed',4000,'EMPTY_FEED_TIMEOUT')]
    stages[2] += native([a for a in feed_polls if a.get('request') == '04 00 34 00 01'])
    stages[2] += native([raw_write('Relay',0x08,0,5)])
    stages[2] += [move('empty','entry'), move('empty'), ('coil','Gripper',1)]
    stages[2] += native([a for a in feed_polls if a.get('request') != '04 00 34 00 01'])
    stages[3] = [('present','ContainerSensor',2000,'NO_CONTAINER')] + native(without_old_moves(old[2]))
    stages[4] = [move('empty','entry'), move('rinse','entry'), move('rinse')]
    stages[4] += native(without_old_moves(old[3])) + [move('rinse','entry')]
    stages[5] = [move('fill','entry'), move('fill')]
    stages[5] += native([a for a in old[4] if a['type'] == 'loopUntilResponse'])
    stages[6] = native([raw_write('FlowMeter',1,0)])
    stages[6] += [('volume',r['fill_device'],r['volume_ml']), ('fill_time',r['fill_device'],r['fill_time_ms']),
                 ('coil',r['fill_device'],1), ('ready',r['fill_device'],20000,'FILL_TIMEOUT')]
    stages[6] += native([flow_check, level]) + [('coil',r['fill_device'],0)]
    stages[6] += [('repeat_legacy',4,[pressure])] + native([flow_cache]) + [('read_volume',r['fill_device'])]
    stages[6] += native([{'name':'Fill done','type':'updateUI','uiTarget':'FillState','uiValue':'done'}])
    stages[7] = native(without_old_moves(old[6]))
    stages[8] = [move('fill','entry'), move('closure','entry'), move('closure'), ('coil','CapFeeder',1),
                 ('trigger','Closure',r['closure_pr']), ('ready','Closure',5000,'CLOSURE_TIMEOUT')]
    stages[8] += native(without_old_moves(old[7])) + [('coil','CapFeeder',0), move('closure','entry')]
    stages[9] = [move('code','entry'), move('code')] + native(without_old_moves(old[8])) + [move('code','entry')]
    stages[10] = [move('finished','entry'), move('finished'), ('coil','Gripper',0)]
    stages[10] += native([raw_write('Relay',0x08,1,5)])
    stages[10] += [('trigger','Outfeed',r['outfeed_pr']), ('ready','Outfeed',4000,'OUTFEED_TIMEOUT')]
    stages[10] += native([a for a in old[9] if a['type'] in ('loopUntilResponse','readAndCache') and a.get('device') not in MOTORS[:4]])
    stages[10] += native([raw_write('Relay',0x08,0,5)])
    stages[10] += [move('finished','entry'), ('count',)]
    stages[11] = native(without_old_moves(old[10]))
    stages[12] = native(reset) + home + native(alarms)
    stages[12] += native([{'name':'Standby','type':'updateUI','uiTarget':'SystemState','uiValue':'standby'}])
    return {i: [('legacy',marker(i))]+body for i, body in stages.items()}


class FullSuite(LayeredSuite):
    def action(self, name, kind, args, result='', arg_constraints=None, **fields):
        if kind == 'modbus_read_cache' and 'response' in fields:
            name += '_expected'
        return super().action(name,kind,args,result,arg_constraints,**fields)

    def render(self, plan):
        body = []
        for kind, *args in plan:
            if kind == 'legacy':
                a = args[0]
                if a['type'] == 'setVariable': body.append(self.setvar(a['key'],a['value']))
                else: body.append(self.leaf(a, 'restored_stage'))
            elif kind == 'repeat_legacy':
                body.append({'type':'group','name':'Repeated diagnostic readings','mode':'loop',
                             'loop':{'type':'count','count':args[0]},
                             'body':self.render([('legacy',a) for a in args[1]])})
            elif kind == 'fill_time': body.append(self.write(args[0],0x0011,args[1]))
            elif kind == 'refresh': body.append(self.call_group('refresh_position'))
            else: body.extend(self.render_plan([(kind,*args)]))
        return body


def render_raw(plan, points):
    out = []
    for kind, *args in plan:
        if kind == 'legacy': out.append(copy.deepcopy(args[0]))
        elif kind == 'repeat_legacy': out.append({'type':'repeat','count':args[0],'actions':copy.deepcopy(args[1])})
        elif kind == 'fill_time': out.append(raw_write(args[0],0x0011,args[1]))
        elif kind == 'refresh':
            out += [raw_read(m,0x602C,m+'_position',2) for m in MOTORS[:4]]
            out += [{'name':'Refresh '+a,'type':'updateUI','uiTarget':'center'+a,'uiValue':'${'+m+'_position}'}
                    for a,m in [('X','X'),('Y','Y1'),('Z','Z')]]
        else: out.extend(expand_raw([(kind,*args)], points))
    return out


def build_phase(path, include_small, old):
    recipes, registry, variables = specification(include_small)
    save(path/'device_registry.json',registry)
    save(path/'system_variables.json',variables)
    suite = FullSuite()
    suite.build_templates()
    symbolic = {k:'${recipes[recipe_index].'+k+'}' for k in recipes[0]}
    stages = stage_plans(symbolic,old)
    for i, name in enumerate(STAGES,1):
        suite.define(f'step_{i:02d}_'+name,suite.render(stages[i]))
    def call(i): return suite.call_group(f'step_{i:02d}_'+STAGES[i-1])
    suite.define('produce_one_container',[call(i) for i in range(2,11)])
    suite.define('produce_batch',[suite.call_group('produce_one_container')],mode='loop',loop={'type':'count','count':6})
    setup = [suite.setvar('recipe_index','${recipe_lookup[flavor][specification]}'), suite.setvar('completed',0)]
    setup += [suite.setvar('last_'+a+'_pr',-1) for a in 'xyz']
    suite.define('run_recipe',setup+[call(1),{'type':'group','name':'Production batches','mode':'loop',
        'loop':{'type':'count','count':2},'body':[suite.call_group('produce_batch')]},call(11),call(12)],
        [('flavor','u16'),('specification','u16')])
    for a in suite.groups['run_recipe.group.json']['args']: a.update(min=0,max=2)
    suite.groups['run_recipe.group.json']['args'][1]['max'] = 2 if include_small else 1
    for r in recipes:
        concrete = stage_plans(r,old)
        body = [{'type':'setVariable','key':'completed','value':0}]
        body += [{'type':'setVariable','key':'last_'+a+'_pr','value':-1} for a in 'xyz']
        body += render_raw(concrete[1],variables['production_points'])
        inner = sum((render_raw(concrete[i],variables['production_points']) for i in range(2,11)),[])
        body += [{'type':'repeat','count':2,'actions':[{'type':'repeat','count':6,'actions':inner}]}]
        body += render_raw(concrete[11]+concrete[12],variables['production_points'])
        save(path/'legacy'/(r['id']+'.json'),{'schema':'synthetic_monolithic_12stage_v1','name':r['id'],'recipe':r,'actions':body})
        params = [[f for f,_,_ in FLAVORS].index(r['flavor']),[s for s,_,_ in FORMATS].index(r['format'])]
        save(path/'layered/L4_flow'/(r['id']+'.json'),{'type':'flow','name':r['id'],'mode':'sequence',
             'body':[suite.call_group('run_recipe',params)]})
    save(path/'layered/L1_action/all_actions.json',{'type':'action_bundle','actions':list(suite.actions.values())})
    save(path/'layered/L2_node/all_nodes.json',{'type':'node_bundle','nodes':list(suite.nodes.values())})
    for name,value in suite.groups.items(): save(path/'layered/L3_group'/name,value)
    return {'recipes':len(recipes),'points':len(variables['production_points']),'devices':len(registry['devices']),
            'L1':len(suite.actions),'L2':len(suite.nodes),'L3':len(suite.groups)}


def build(output, legacy_source):
    if output.exists() and any(output.iterdir()): raise ValueError('Use an empty output directory')
    old = old_steps(legacy_source)
    assert len(old) == 12
    return {name:build_phase(output/name,small,old) for name,small in [('example4',False),('example5',True)]}


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'examples/beverage_full')
    parser.add_argument('--legacy-source',type=Path,default=ROOT/'examples/beverage_legacy/beverage_filling_legacy.json')
    a=parser.parse_args()
    print(json.dumps(build(a.output,a.legacy_source),indent=2))
