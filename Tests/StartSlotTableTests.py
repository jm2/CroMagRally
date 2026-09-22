#!/usr/bin/env python3
"""Checks Source/Terrain/StartSlotTable.c, the generated start slots for players 6-11.

Re-validates every slot in the checked-in table against the shipped playfields (spacing,
fences, flatness and height, water, obstacles, map bounds, a clear run ahead, no car on
another's nose, CTF sides, the checkpoint rule),
checks that the table is exactly what tools/gen_start_slots.py generates today, and checks
that the validator rejects each kind of bad slot. Uses only the standard library.

usage: StartSlotTableTests.py <repository root>
"""

import copy
import os
import sys
import unittest

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..'))
sys.dont_write_bytecode = True                  # keep tools/ free of __pycache__
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import gen_start_slots as gen  # noqa: E402

TABLE = os.path.join(ROOT, 'Source', 'Terrain', 'StartSlotTable.c')


class StartSlotTableTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        with open(TABLE) as f:
            cls.text = f.read()
        cls.entries = gen.parse_table(cls.text)
        cls.maps = {}

    def map(self, name):
        if name not in self.maps:
            self.maps[name] = gen.MapData(name, os.path.join(ROOT, 'Data', 'Terrain'))
        return self.maps[name]

    def problems_with(self, name, set_name, player, x, z, rot16=None):
        """Validator output after moving one table slot of (name, set_name) to (x, z)."""
        entries = copy.deepcopy(self.entries)
        for entry in entries:
            if entry[0] == name and entry[1] == set_name:
                old = entry[3][player - gen.AUTHORED]
                entry[3][player - gen.AUTHORED] = (int(x), int(z), old[2] if rot16 is None else rot16)
        return gen.check_entries(entries, os.path.join(ROOT, 'Data', 'Terrain'), maps=[name])

    def assertRejected(self, problems, *needles):
        self.assertTrue(any(n in p for p in problems for n in needles),
                        'expected a problem containing %s, got %s' % (' or '.join(repr(n) for n in needles), problems))

    # ---- the checked-in table --------------------------------------------------------------

    def test_table_has_every_map_and_set(self):
        expected = [(n, gen.SET_RACE) for n in gen.RACE_MAPS] + \
                   [(n, s) for n in gen.ARENA_MAPS for s in (gen.SET_BATTLE, gen.SET_CTF)]
        self.assertEqual([(e[0], e[1]) for e in self.entries], expected)

    def test_every_slot_passes(self):
        self.assertEqual(gen.check_table(self.text, os.path.join(ROOT, 'Data', 'Terrain')), [])

    def test_twelve_distinct_slots_per_set(self):
        for name, set_name, authored, extra in self.entries:
            positions = [(x, z) for x, z, _ in authored + extra]
            self.assertEqual(len(positions), 12, name)
            self.assertEqual(len(set(positions)), 12, '%s %s has duplicate slots' % (name, set_name))

    def test_table_is_up_to_date(self):
        self.assertEqual(gen.format_table(gen.generate_all(os.path.join(ROOT, 'Data', 'Terrain'))), self.text,
                         'StartSlotTable.c is stale: run python3 tools/gen_start_slots.py')

    def test_no_numpy_needed(self):
        self.assertNotIn('numpy', sys.modules)

    # ---- the validator rejects bad slots ------------------------------------------------------

    def test_rejects_crowded_slot(self):
        x, z, _ = self.entries[0][2][0]
        self.assertRejected(self.problems_with('StoneAge_Desert', gen.SET_RACE, 7, x - 500, z), 'only 500 from p0')

    def test_rejects_off_map(self):
        self.assertRejected(self.problems_with('Battle_Coliseum', gen.SET_BATTLE, 6, 100, 100), 'off the map')

    def test_rejects_hedge(self):
        md = self.map('Battle_Maze')
        auth = md.authored(False)
        cx, cz = sum(s.x for s in auth) / 6, sum(s.z for s in auth) / 6
        top = max(((md.pf.terrain_y(cx + i * 200, cz + j * 200), cx + i * 200, cz + j * 200)
                   for i in range(-25, 26) for j in range(-25, 26)))
        self.assertGreater(top[0], 1000)          # a hedge top, well above the corridors
        self.assertRejected(self.problems_with('Battle_Maze', gen.SET_BATTLE, 6, top[1], top[2]),
                            'height range', 'from its source', 'terrain rises', 'steep')

    def test_footprint_height_range_is_exact(self):
        pf = self.map('StoneAge_Desert').pf
        for x, z in ((72200, 86128), (70150, 87105), (41234.5, 60321.25)):
            lo, hi = pf.height_range(x, z, gen.FOOTPRINT_R)
            dense = [pf.terrain_y(x + i * 20, z + j * 20) for i in range(-20, 21) for j in range(-20, 21)
                     if (i * 20) ** 2 + (j * 20) ** 2 <= gen.FOOTPRINT_R ** 2]
            self.assertLessEqual(lo, min(dense) + 1e-6)
            self.assertGreaterEqual(hi, max(dense) - 1e-6)
            self.assertLess((hi - lo) - (max(dense) - min(dense)), 20)     # and no looser than sampling

    def test_rejects_rough_footprint_between_grid_samples(self):
        # The first table's Desert p10, at the foot of the canyon wall: 278 on a 100-unit grid,
        # 328 exactly.
        self.assertRejected(self.problems_with('StoneAge_Desert', gen.SET_RACE, 10, 72200, 86128),
                            'footprint height range 328')

    def test_rejects_wall_late_in_the_clear_run(self):
        # The first table's China p11 faced a 63% bank 950 ahead, though the average grade over
        # the whole run was only 21%.
        self.assertRejected(self.problems_with('BronzeAge_China', gen.SET_RACE, 11, 24350, 71750), 'wall 950 ahead')

    def test_rejects_slot_inside_the_battle_ring(self):
        # The first table's Spiral p7, where four authored cars' headings converged.
        self.assertRejected(self.problems_with('Battle_Spiral', gen.SET_BATTLE, 7, 27700, 23100, 7),
                            'inside the authored ring')

    def test_rejects_car_on_a_nose(self):
        md = self.map('Battle_Coliseum')
        p0 = md.authored(False)[0]
        f = gen.heading(p0.rot16)
        spot = (p0.x - f[1] * 1500, p0.z + f[0] * 1500)            # 1500 beside p0, facing it
        rot = gen.rot16_towards(p0.x - spot[0], p0.z - spot[1])
        self.assertRejected(self.problems_with('Battle_Coliseum', gen.SET_BATTLE, 6, spot[0], spot[1], rot),
                            'p0 is on its nose')

    def test_on_nose_rule(self):
        car = gen.Slot(0, 0, 0, 0)                                  # faces -z
        self.assertTrue(gen.on_nose(car, gen.Slot(1, 100, -2900, 8)))      # head-on, 2900 ahead
        self.assertFalse(gen.on_nose(car, gen.Slot(1, 100, -3100, 8)))
        self.assertTrue(gen.on_nose(car, gen.Slot(1, -400, -1900, 4)))     # crossing, 1900 ahead
        self.assertFalse(gen.on_nose(car, gen.Slot(1, -400, -2100, 4)))
        self.assertFalse(gen.on_nose(car, gen.Slot(1, 600, -1000, 4)))     # beside the path
        self.assertFalse(gen.on_nose(car, gen.Slot(1, 0, -1000, 2)))       # following, 45 degrees off
        self.assertFalse(gen.on_nose(car, gen.Slot(1, 0, 1000, 8)))        # behind

    def test_rejects_water(self):
        md = self.map('BronzeAge_Egypt')
        wet = next((x, z) for x in range(118000, 126000, 200) for z in range(74000, 80000, 200)
                   if md.liquid_at(x, z) == 'water')
        self.assertRejected(self.problems_with('BronzeAge_Egypt', gen.SET_RACE, 7, *wet), 'in water')

    def test_rejects_obstacle(self):
        md = self.map('BronzeAge_Egypt')
        pillar = next((x, z) for x, z, r, label in md.circles if label == 'pillar')
        self.assertRejected(self.problems_with('BronzeAge_Egypt', gen.SET_RACE, 7, *pillar), 'clearance of a pillar')

    def test_rejects_fence_between(self):
        md = self.map('StoneAge_Desert')
        src = md.authored(False)[0]
        lat = (-gen.heading(src.rot16)[1], gen.heading(src.rot16)[0])
        d = next(d for d in range(500, 20000, 100)
                 if md.fence_between(src.pos, (src.x + lat[0] * d, src.z + lat[1] * d)))
        spot = (src.x + lat[0] * (d + 1000), src.z + lat[1] * (d + 1000))
        self.assertRejected(self.problems_with('StoneAge_Desert', gen.SET_RACE, 6, *spot), 'fence between')

    def test_rejects_slot_behind_checkpoint_n_minus_2(self):
        md = self.map('IronAge_Europe')
        src = md.authored(False)[0]
        (ax, az), (bx, bz) = md.pf.checkpoints[-2]
        mx, mz = (ax + bx) / 2, (az + bz) / 2
        ln = gen.dist((mx, mz), src.pos)
        spot = (mx + (mx - src.x) / ln * 1500, mz + (mz - src.z) / ln * 1500)
        self.assertRejected(self.problems_with('IronAge_Europe', gen.SET_RACE, 6, *spot),
                            'behind checkpoint %d' % (len(md.pf.checkpoints) - 2))

    def test_allows_only_checkpoint_n_minus_1(self):
        md = self.map('IronAge_Europe')
        n = len(md.pf.checkpoints)
        ctx = gen.Context(md, gen.SET_RACE, md.authored(False))
        entry = next(e for e in self.entries if e[0] == 'IronAge_Europe')
        crossed = set()
        for i, (x, z, r) in enumerate(entry[3]):
            slot = gen.Slot(gen.AUTHORED + i, x, z, r, source=ctx.authored[i])
            crossed.update(md.checkpoints_between(slot.pos, slot.source.pos))
        self.assertEqual(crossed, {n - 1})       # Europe's extra rows start behind N-1 only

    def test_rejects_race_slot_in_front_of_the_grid(self):
        x, z, _ = self.entries[0][2][4]
        self.assertRejected(self.problems_with('StoneAge_Desert', gen.SET_RACE, 11, x + 2000, z), 'behind the authored grid')

    def test_rejects_turned_race_slot(self):
        x, z, r = self.entries[0][3][0]
        self.assertRejected(self.problems_with('StoneAge_Desert', gen.SET_RACE, 6, x, z, (r + 4) & 15), 'heading differs')

    def test_rejects_lone_human_slot_ahead_of_its_wave(self):
        entries = copy.deepcopy(self.entries)
        entry = next(e for e in entries if e[0] == 'BronzeAge_Egypt')
        entry[3][0], entry[3][4] = entry[3][4], entry[3][0]      # slot 6 <-> a front-row slot
        self.assertRejected(gen.check_entries(entries, os.path.join(ROOT, 'Data', 'Terrain'), maps=['BronzeAge_Egypt']),
                            'from the back of its wave')

    def test_rejects_ctf_slot_on_the_other_half(self):
        entry = next(e for e in self.entries if e[0] == 'Battle_Coliseum' and e[1] == gen.SET_CTF)
        x, z, _ = entry[2][1]                   # a green (odd) authored slot
        self.assertRejected(self.problems_with('Battle_Coliseum', gen.SET_CTF, 6, x - 1500, z), 'into its own half')

    def test_rejects_bad_heading(self):
        x, z, _ = self.entries[0][3][0]
        self.assertRejected(self.problems_with('StoneAge_Desert', gen.SET_RACE, 6, x, z, 17), 'is not 0-15')

    def test_rejects_changed_fingerprint(self):
        entries = copy.deepcopy(self.entries)
        x, z, r = entries[0][2][3]
        entries[0][2][3] = (x + 50, z, r)
        self.assertRejected(gen.check_entries(entries, os.path.join(ROOT, 'Data', 'Terrain'), maps=['StoneAge_Desert']),
                            'fingerprint does not match')


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], '-v'])
