"""Regression and negative controls for the twelve-stage parameterized line."""
import copy
from pathlib import Path
import unittest
from unittest.mock import patch
import simulate_beverage_suite as base
import simulate_beverage_full as full

ROOT = Path(__file__).resolve().parents[1] / 'examples/beverage_full'


class FullProcessTests(unittest.TestCase):
    def reject(self, filename, mutate):
        path = ROOT / 'example5/layered/L3_group' / filename
        original_read = base.read
        data = copy.deepcopy(original_read(path))
        mutate(data)
        with patch.object(base,'read',side_effect=lambda p: copy.deepcopy(data) if Path(p).resolve()==path.resolve() else original_read(p)):
            with self.assertRaises((AssertionError,ValueError,KeyError)):
                full.run_pair(ROOT/'example5',6)

    def test_rinse_duration_is_executed(self):
        def mutate(data):
            wait = next(n for n in data['body'] if n.get('params') == [5000])
            wait['params'] = [1]
        self.reject('step_04_in_bottle_rinsing.group.json',mutate)

    def test_cip_duration_is_executed(self):
        def mutate(data):
            wait = next(n for n in data['body'] if n.get('params') == [120000])
            wait['params'] = [1]
        self.reject('step_11_cip_cleaning_sanitizing.group.json',mutate)

    def test_all_level_rechecks_are_executed(self):
        def mutate(data):
            at = next(i for i,n in enumerate(data['body']) if n.get('type')=='group' and 'poll_sensor' in n.get('template',''))
            data['body'].pop(at)
        self.reject('step_07_level_recheck.group.json',mutate)

    def test_label_is_executed(self):
        def mutate(data):
            data['body'] = [n for n in data['body'] if not ('write_05000d' in n.get('template','') and n.get('params')==[65280])]
        self.reject('step_09_coding_labeling.group.json',mutate)

    def test_extension_keeps_baseline(self):
        before,after=ROOT/'example4',ROOT/'example5'
        for p in before.rglob('*.json'):
            if p.name not in ('system_variables.json','run_recipe.group.json'):
                self.assertEqual(p.read_bytes(),(after/p.relative_to(before)).read_bytes())
        a,b=(base.read(p/'layered/L3_group/run_recipe.group.json') for p in (before,after))
        self.assertEqual(a['args'][1]['max'],1)
        self.assertEqual(b['args'][1]['max'],2)
        a['args'][1]['max']=2
        self.assertEqual(a,b)
        a,b=(base.read(p/'system_variables.json') for p in (before,after))
        self.assertEqual(a['recipes'],b['recipes'][:6])
        self.assertEqual(a['production_points'],b['production_points'][:len(a['production_points'])])
        for x,y in zip(a['recipe_lookup'],b['recipe_lookup']): self.assertEqual(x,y[:2])

    def test_unavailable_specification_stops_before_io(self):
        root=ROOT/'example4'
        variables=base.read(root/'system_variables.json')
        line=full.FullVirtualLine(base.read(root/'device_registry.json'),variables['recipes'][0])
        interpreter=base.LayeredInterpreter(root/'layered',line,variables)
        flow=base.read(root/'layered/L4_flow/orange_family.json')
        flow['body'][0]['params']=[0,2]
        with self.assertRaises(ValueError): interpreter.run(flow)
        self.assertFalse(any(e['kind']=='modbus' for e in line.trace))


if __name__=='__main__': unittest.main()
