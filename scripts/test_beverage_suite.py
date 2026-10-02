"""Negative controls: changing only the saved four-layer configuration must be detected."""
import copy
from pathlib import Path
import unittest
from unittest.mock import patch
import simulate_beverage_suite as sim

ROOT = Path(__file__).resolve().parents[1] / "examples/beverage_suite"


class SuiteTests(unittest.TestCase):
    def reject(self, relative, mutate, recipe=1):
        target = (ROOT / relative).resolve()
        changed = copy.deepcopy(sim.read(target))
        mutate(changed)
        original = sim.read
        with patch.object(sim, "read", side_effect=lambda p: copy.deepcopy(changed) if Path(p).resolve() == target else original(p)):
            with self.assertRaises((AssertionError, ValueError, KeyError)):
                sim.run_pair(ROOT, recipe)

    def test_required_matrix(self):
        recipes = sim.read(ROOT / "system_variables.json")["recipes"]
        self.assertEqual(len(recipes), 9)
        self.assertEqual({(r["flavor"], r["format"]) for r in recipes},
                         {(f, s) for f in ("orange", "apple", "grape") for s in ("family", "can", "small")})
        self.assertEqual({r["format"]: r["volume_ml"] for r in recipes}, {"family": 2000, "can": 330, "small": 250})
        for recipe in recipes:
            self.assertEqual(len({recipe[k] for k in ("empty_work", "fill_work", "closure_work", "finished_work")}), 4)

    def test_missing_batch_unit(self):
        self.reject("layered/L3_group/produce_batch.group.json", lambda j: j["loop"].__setitem__("count", 5))

    def test_wrong_recipe_selection(self):
        self.reject("layered/L4_flow/orange_can.json", lambda j: j["body"][0]["params"].__setitem__(0, 0))

    def test_missing_y2(self):
        self.reject("layered/L3_group/move_production_point.group.json", lambda j: j["body"][1]["params"][2].pop())

    def test_poll_interval_changes_timing(self):
        self.reject("layered/L3_group/poll_sensor_until_ready.group.json", lambda j: j["body"][1]["then"][0]["params"].__setitem__(0, 21))

    def test_wrong_fill_register(self):
        def mutate(j):
            action = next(a for a in j["actions"] if a.get("request", "").startswith("06 00 10"))
            action["request"] = action["request"].replace("06 00 10", "06 00 11")
            action["response"] = action["request"]
        self.reject("layered/L1_action/all_actions.json", mutate)

    def test_wrong_cache_decoder(self):
        def mutate(j):
            action = next(a for a in j["actions"] if a.get("request") == "03 60 2C 00 02")
            action["parse"]["endian"] = "little"
        self.reject("layered/L1_action/all_actions.json", mutate)

    def test_expected_fault_outcomes(self):
        for fault, status in [("infeed_jam", "EMPTY_FEED_TIMEOUT"), ("fill_offline", "COMM_OFFLINE"), ("closure_jam", "CLOSURE_TIMEOUT")]:
            with self.subTest(fault=fault): self.assertEqual(sim.run_pair(ROOT, 0, fault)["status"], status)


if __name__ == "__main__": unittest.main()
