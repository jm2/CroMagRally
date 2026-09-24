#!/usr/bin/env python3
"""Generate the explicit start slots for players 6-11 on the 17 shipped playfields.

Every shipped playfield authors exactly six MyStartCoord items per slot set: the race grid on
the 9 tracks, and on the 8 arenas a battle ring (Tag, Survival) plus six Capture the Flag slots
(parm[3] bit 0, three per team, even players red, odd green). This script derives six more
slots per set from the map data and writes them to Source/Terrain/StartSlotTable.c, keyed by
the exact authored slots so the game can tell when a map was modified and fall back to its
procedural rule (Source/Terrain/StartSlots.c).

  race    The procedural rule: repeat the authored grid behind itself (shift = grid depth span
          + 1300 along -forward), same heading, slot 6+k behind authored slot k, so the game can
          swap each human into the rearmost wave. Where that wave breaks a constraint, the whole
          wave moves further back or sideways, its lanes squeeze or widen and its rows stagger
          sideways to follow a road that bends behind the grid; the passing wave with the lowest
          cost (distance moved plus a penalty for rough ground) wins. If no whole wave passes,
          single slots move to the best spot nearby.
  battle  Each slot keeps the procedural rule's spot (a second, wider ring rotated half a slot)
          if that passes, else takes the best passing spot nearby: close to the rule's spot, on
          flat ground, with room around it, outside the authored ring. Slots face the ring's
          centre, and no two cars start on each other's nose.
  ctf     Likewise from the rule's spot (a column beside each team's line, towards the arena),
          on the team's own half of the arena, with the team's heading.

Every generated slot must pass check_slot() (thresholds and where they come from are documented
below); the generator fails if it cannot place one. Tests/StartSlotTableTests.py re-validates
the checked-in table against the map data and checks that the table is up to date.

Usage:
  python3 tools/gen_start_slots.py              write Source/Terrain/StartSlotTable.c
  python3 tools/gen_start_slots.py --check      exit 1 if the table is stale or a slot fails
  python3 tools/gen_start_slots.py --report     print every generated slot's constraint metrics
  python3 tools/gen_start_slots.py --render DIR before/after previews (needs numpy + Pillow)

Everything except --render uses only the standard library.
"""

import argparse
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import terlib  # noqa: E402

REPO = os.path.dirname(HERE)
TERRAIN_DIR = os.path.join(REPO, 'Data', 'Terrain')
TABLE_PATH = os.path.join(REPO, 'Source', 'Terrain', 'StartSlotTable.c')

RACE_MAPS = ['StoneAge_Desert', 'StoneAge_Jungle', 'StoneAge_Ice',            # TRACK_NUM order
             'BronzeAge_Crete', 'BronzeAge_China', 'BronzeAge_Egypt',
             'IronAge_Europe', 'IronAge_Scandinavia', 'IronAge_Atlantis']
ARENA_MAPS = ['Battle_StoneHenge', 'Battle_Aztec', 'Battle_Coliseum', 'Battle_Maze',
              'Battle_Celtic', 'Battle_TarPits', 'Battle_Spiral', 'Battle_Ramps']

SET_RACE, SET_BATTLE, SET_CTF = 'race', 'battle', 'ctf'
SET_ENUM = {SET_RACE: 'START_SLOT_SET_RACE', SET_BATTLE: 'START_SLOT_SET_BATTLE',
            SET_CTF: 'START_SLOT_SET_CTF'}

AUTHORED = 6            # authored slots per set on every shipped map (players 0-5)
EXTRA = 6               # table slots per set (players 6-11)

# ---- constraints ------------------------------------------------------------------------------
#
# Distances and heights are world units. "Authored" figures are over the 150 shipped start
# slots (9 grids x 6, 8 arenas x 12), each against its nearest neighbour of the same set (and
# team, for CTF).

MIN_SPACING = 900       # from every other slot of the same set, authored or generated. (Desert's
                        # own grid has one pair 856 apart; every other authored pair is >= 900.)
MAP_EDGE = 1600         # two terrain tiles inside the playfield edge
FENCE_CLEARANCE = 800   # from every fence: a car length plus room to steer (authored: >= 1440)
FOOTPRINT_R = 400       # footprint radius checked for flatness, water and blank supertiles: the
                        # car's collision box is +-120 (Player_Car.c), plus room around it
MAX_ROUGH = 300         # footprint height range (exact, terlib height_range). Authored: median 40,
                        # p95 253; only four exceed 300 (Europe p2 at 330, two TarPits slots, a
                        # Maze CTF slot by a hedge). A hedge flank or ridge side reads 700-1600.
MAX_DH = 300            # |height - source height| <= MAX_DH + MAX_GRADE * distance. Authored
MAX_GRADE = 0.15        # neighbours: p95 258, max 393 at 1500 apart (TarPits). The grade term
                        # lets a race slot 3-5k behind its source follow the track's climb;
                        # hedge and ridge tops sit 1000-5000 above the floor beside them.
MAX_HUMP = 100          # terrain between a slot and its source never rises this far above the
                        # higher end (authored max 19): no hedge, ridge or bank in between
MAX_STEP_GRADE = 0.35   # nor climbs or drops steeper than this over any 200-unit stretch (authored
                        # neighbours: median 0.07, p95 0.36): no terrace edge or cliff in between
# The heading must have this much drivable ground ahead: no fence, map edge or obstacle, no
# 200-unit stretch anywhere along it climbing steeper than MAX_STEP_GRADE (canyon walls and banks
# read 40-65%, hedges ~100%), and none dropping steeper than MAX_DROP_GRADE. Authored race slots
# have >= 2000 before the first such climb (Atlantis 1450); authored arena slots >= 1500, except
# Maze battle p4 (1350) and four CTF slots that start by a hedge or on a slope (Maze p0, TarPits
# p1 and p5, Ramps p3).
CLEAR_RUN = {'race': 2000, 'battle': 1500, 'ctf': 1500}
MAX_DROP_GRADE = 0.5    # a car rolls down a slope, so drops only matter at a cliff edge: authored
                        # slots face drops of up to 39% within their clear run (TarPits' pit rims,
                        # Atlantis p3); cliff and bank edges read 75-100%
# No generated slot may sit on another car's nose, or have one on its own: within PATH_RANGE ahead
# and PATH_WIDTH to either side of the heading, unless both face within 2/16 turn of each other
# (a car following another, as on a race grid), and within HEADON_RANGE when they face each other
# (7/16 turn or more apart). Authored rings: the nearest car on a nose is 1350 ahead (Spiral p3 to
# p4), then 1945 and 2157; cars facing each other are >= 3050 apart.
PATH_RANGE, PATH_WIDTH, HEADON_RANGE = 2000, 500, 3000
RACE_GAP = 1300         # the procedural rule's gap between the authored grid and its copy
RACE_BEHIND = 900       # race slots stay this far behind the rearmost authored slot, so every
                        # human swapped into them starts behind every CPU on an authored slot
ROW_TOL = 400           # slot 6 (where StartSlots_KeepHumansAtBack first puts a lone human) stays
                        # in its wave's rear row: no further from the wave's back than player 0 is
                        # from the authored grid's back, plus this tolerance
CTF_SIDE_MARGIN = 500   # CTF slots stay this far inside their team's half of the arena
SEARCH_STEP = 100       # candidate grid for slots that have to move
SEARCH_RADIUS = 6000    # how far a failing slot may move
# A slot that has to move goes to the passing candidate with the lowest score: its distance from
# the rule's spot plus these penalties, so it settles comfortably inside the valid area instead of
# on its edge (e.g. right beside a hedge, or exactly MIN_SPACING from a neighbour).
COMFORT_ROUGH, ROUGH_PENALTY = 100, 5           # per unit of footprint height range above 100
COMFORT_SPACING, SPACING_PENALTY = 1200, 2      # per unit closer than 1200 to another slot
TURN_PENALTY = 300                              # per 1/16 turn away from the preferred heading

# Solid and visual terrain items. Clearance from the item's centre = footprint + CAR_MARGIN,
# where the footprint is the half-diagonal of the collision box the item's Add* routine builds
# (Items.c, Triggers.c, Traps.c), or of its model for items cars pass through. Model sizes were
# measured from the Data/Models/*.bg3d bounding boxes of the variants the shipped maps use.
CAR_MARGIN = 400
ITEM_FOOTPRINT = {
    1: ('cactus', 305), 3: ('sign', 250), 7: ('vase', 334), 9: ('flag pole', 300),
    14: ('Easter Island head', 438), 16: ('snowman', 424), 17: ('campfire', 500),
    21: ('pylon', 1074), 22: ('boat', 4166), 24: ('statue', 660), 26: ('team torch', 141),
    27: ('team base', 695), 30: ('rock', 565), 34: ('Aztec head', 772), 39: ('house', 4638),
    51: ('cannon', 749), 63: ('druid', 1100),
    # Trees collide only at the trunk (+-50, AddTree), but a car should not start inside the
    # lower branches: 900 covers the palms, the Crete and Aztec trees and the pines' trunks.
    4: ('tree', 900),
}
# Pillars (AddPillar) use a different model per track.
PILLAR_FOOTPRINT = {'StoneAge_Desert': 1363, 'BronzeAge_Crete': 569, 'IronAge_Scandinavia': 629,
                    'IronAge_Atlantis': 849, 'Battle_Coliseum': 1071,
                    'BronzeAge_Egypt': {0: 216, 1: 721}}          # Egypt: pillar, obelisk
ITEM_PILLAR = 20
DEFAULT_FOOTPRINT = 1100    # anything else (none sits near a start area)
# Not obstacles: start coords, POWs and tokens (pickups), the finish line, the waterfall, spline
# creatures (the item only anchors a spline), bubble generators, hanging vines, and the
# Coliseum wall model (the arena fence is its collision).
IGNORED_ITEMS = {0, 5, 6, 10, 11, 12, 13, 18, 23, 28, 29, 33, 35, 46, 53, 54, 58, 61, 64, 66}
# Team torches and bases exist only in Capture the Flag (AddTeamTorch, AddTeamBase return early in
# every other mode), so they are obstacles only for CTF slots.
CTF_ONLY_ITEMS = {26, 27}
ITEM_STONEHENGE = 45
HENGE_PYLON_RADIUS = 900    # Items.c: collision boxes of the inner and outer henge stones
HENGE_POST_FOOTPRINT = 839  # stonehenge.bg3d Post

# Liquids: a car is in a liquid when it is inside the patch's box and the ground is below the
# surface (Player_Car.c). Water patches (Liquids.c AddWaterPatch) cover +-2 tiles around the
# centre of their tile; the surface is gWaterHeights[track][parm[0]] for fixed-height patches
# (parm[3] bit 0), else 100 above the ground at the centre. Tar patches (AddTarPatch) are the
# +-1600 tar model scaled by 1 + parm[0] / 2, with the surface 150 above the ground.
ITEM_WATER, ITEM_TAR = 2, 60
WATER_HEIGHTS = {'StoneAge_Jungle': 700, 'BronzeAge_Crete': 600, 'BronzeAge_Egypt': 250}   # [0]
LIQUID_MARGIN = 50      # ground less than this above the surface counts as wet

# Atlantis races submarines (InitPlayer_Submarine): they spawn 500 above the seabed and are held
# at least 200 above it (Player_Submarine.c), so ground flatness, the height difference from the
# source slot, the steepness in between and drops ahead do not apply there. Ridges in between
# and walls ahead still do.
SUBMARINE_MAPS = {'IronAge_Atlantis'}


# Car forward (-sin rot, -cos rot) for the 16 headings, as literals: the generator must give the
# same table on every platform, and libm sin/cos/atan2 may differ in the last bit. Only IEEE
# basic arithmetic and sqrt (correctly rounded everywhere) are used below.
_S1, _S2, _S3 = 0.3826834323650898, 0.7071067811865476, 0.9238795325112867     # sin 22.5/45/67.5
_HEADINGS = [(0.0, -1.0), (-_S1, -_S3), (-_S2, -_S2), (-_S3, -_S1), (-1.0, 0.0), (-_S3, _S1),
             (-_S2, _S2), (-_S1, _S3), (0.0, 1.0), (_S1, _S3), (_S2, _S2), (_S3, _S1), (1.0, 0.0),
             (_S3, -_S1), (_S2, -_S2), (_S1, -_S3)]


def heading(rot16):
    """Car forward (x, z) for a heading in 1/16 turns."""
    return _HEADINGS[rot16 & 15]


def rot16_towards(dx, dz):
    """The 1/16-turn heading closest to facing (dx, dz) (the lowest one on a tie)."""
    return max(range(16), key=lambda r: (_HEADINGS[r][0] * dx + _HEADINGS[r][1] * dz, -r))


def dist(a, b):
    dx, dz = a[0] - b[0], a[1] - b[1]
    return math.sqrt(dx * dx + dz * dz)


class Slot:
    """A start slot: position in world units and heading in 1/16 turns (MyStartCoord parm[1])."""

    __slots__ = ('player', 'x', 'z', 'rot16', 'team', 'source', 'note')

    def __init__(self, player, x, z, rot16, source=None, note=''):
        self.player, self.x, self.z, self.rot16 = player, int(x), int(z), rot16 & 15
        self.team = player & 1
        self.source = source            # authored Slot this one is derived from / checked against
        self.note = note

    @property
    def pos(self):
        return (self.x, self.z)


_CELL = 2048                # obstacle lookup grid
_FOOTPRINT_CORNERS = [(dx, dz) for dx in (-FOOTPRINT_R, 0, FOOTPRINT_R) for dz in (-FOOTPRINT_R, 0, FOOTPRINT_R)]


class MapData:
    """A playfield plus the obstacle geometry the checks need."""

    def __init__(self, name, terrain_dir=TERRAIN_DIR):
        self.name = name
        self.pf = pf = terlib.Playfield(os.path.join(terrain_dir, name + '.ter'))
        self.is_arena = name.startswith('Battle_')
        self.fence_segs = [(a, b, min(a[0], b[0]), max(a[0], b[0]), min(a[1], b[1]), max(a[1], b[1]))
                           for a, b in pf.fence_segments()]
        self.circles = []            # (x, z, clearance, label, CTF only)
        self.liquids = []            # (x0, z0, x1, z1, surface y, label)
        for it in pf.items:
            t = it['type']
            if t in IGNORED_ITEMS:
                continue
            x, z, parm = it['x'], it['z'], it['parm']
            if t == ITEM_WATER:
                cx = x - x % 800 + 400
                cz = z - z % 800 + 400
                if parm[3] & 1:
                    y = WATER_HEIGHTS.get(name, 0) if parm[0] == 0 else 0
                else:
                    y = pf.terrain_y(cx, cz) + 100
                h = 2 * terlib.TERRAIN_POLYGON_SIZE
                self.liquids.append((cx - h, cz - h, cx + h, cz + h, y, 'water'))
            elif t == ITEM_TAR:
                h = 1600 * (1 + parm[0] * 0.5)
                self.liquids.append((x - h, z - h, x + h, z + h, pf.terrain_y(x, z) + 150, 'tar'))
            elif t == ITEM_STONEHENGE and parm[0] == 1:
                # inner henge: two pylon boxes +-1300 along the item's rotation (parm[1] / 64 turns),
                # rounded to whole units so libm differences between platforms cannot show
                r = 2 * math.pi * parm[1] / 64.0
                ox, oz = round(1300 * math.cos(r)), round(1300 * math.sin(r))
                clear = HENGE_PYLON_RADIUS * math.sqrt(2) + CAR_MARGIN
                for sx, sz in ((ox, -oz), (-ox, oz)):
                    self.circles.append((x + sx, z + sz, clear, 'henge stone', False))
            elif t == ITEM_STONEHENGE:
                fp = HENGE_PYLON_RADIUS * math.sqrt(2) if parm[0] == 2 else HENGE_POST_FOOTPRINT
                self.circles.append((x, z, fp + CAR_MARGIN, 'henge stone', False))
            elif t == ITEM_PILLAR:
                fp = PILLAR_FOOTPRINT.get(name, DEFAULT_FOOTPRINT)
                if isinstance(fp, dict):
                    fp = fp.get(parm[0], DEFAULT_FOOTPRINT)
                self.circles.append((x, z, fp + CAR_MARGIN, 'pillar', False))
            else:
                label, fp = ITEM_FOOTPRINT.get(t, ('item type %d' % t, DEFAULT_FOOTPRINT))
                self.circles.append((x, z, fp + CAR_MARGIN, label, t in CTF_ONLY_ITEMS))
        self.bases = {it['parm'][0]: (it['x'], it['z']) for it in pf.items if it['type'] == 27}
        self._range = {}
        # circles bucketed by grid cell, in item order (so the first hit matches a full scan)
        self._cells = {}
        for c in self.circles:
            cx, cz, r = c[0], c[1], c[2]
            for i in range(int((cx - r) // _CELL), int((cx + r) // _CELL) + 1):
                for j in range(int((cz - r) // _CELL), int((cz + r) // _CELL) + 1):
                    self._cells.setdefault((i, j), []).append(c)

    def authored(self, ctf):
        slots = sorted((s for s in self.pf.starts if s['ctf'] == ctf), key=lambda s: s['player'])
        return [Slot(s['player'], s['x'], s['z'], s['rot16']) for s in slots]

    def sets(self):
        return [SET_BATTLE, SET_CTF] if self.is_arena else [SET_RACE]

    # ---- geometry --------------------------------------------------------------------------

    def fence_between(self, a, b):
        x0, x1 = min(a[0], b[0]), max(a[0], b[0])
        z0, z1 = min(a[1], b[1]), max(a[1], b[1])
        for p, q, sx0, sx1, sz0, sz1 in self.fence_segs:
            if sx1 < x0 or sx0 > x1 or sz1 < z0 or sz0 > z1:
                continue
            if terlib.seg_intersect(a, b, p, q):
                return True
        return False

    def fence_distance(self, p, limit):
        """Distance from p to the nearest fence section, or limit if none is closer."""
        best = limit
        px, pz = p
        for (ax, az), (bx, bz), sx0, sx1, sz0, sz1 in self.fence_segs:
            if sx1 < px - best or sx0 > px + best or sz1 < pz - best or sz0 > pz + best:
                continue
            dx, dz = bx - ax, bz - az
            ln = dx * dx + dz * dz
            t = 0.0 if ln == 0 else max(0.0, min(1.0, ((px - ax) * dx + (pz - az) * dz) / ln))
            best = min(best, dist((px, pz), (ax + t * dx, az + t * dz)))
        return best

    def checkpoints_between(self, a, b):
        return [i for i, (p, q) in enumerate(self.pf.checkpoints) if terlib.seg_intersect(a, b, p, q)]

    def obstacle_at(self, x, z, ctf, slack=0.0):
        """The item whose clearance (x, z) is inside, or None. ctf: the slot is for Capture the
        Flag, the only mode with team torches and bases."""
        for cx, cz, r, label, ctf_only in self._cells.get((int(x // _CELL), int(z // _CELL)), ()):
            if ctf_only and not ctf:
                continue
            if abs(cx - x) < r and abs(cz - z) < r and dist((cx, cz), (x, z)) < r - slack:
                return label
        return None

    def liquid_at(self, x, z):
        """The liquid a car at (x, z) could sit in, or None: its footprint overlaps a patch and
        the lowest ground in the footprint is below that patch's surface (exact and cautious)."""
        near = [lq for lq in self.liquids
                if lq[0] - FOOTPRINT_R <= x <= lq[2] + FOOTPRINT_R and lq[1] - FOOTPRINT_R <= z <= lq[3] + FOOTPRINT_R]
        if not near:
            return None
        low = self.height_range(x, z)[0]
        for x0, z0, x1, z1, surface, label in near:
            if low < surface + LIQUID_MARGIN:
                return label
        return None

    def height_range(self, x, z):
        """Exact (lowest, highest) ground in the footprint around (x, z), cached."""
        v = self._range.get((x, z))
        if v is None:
            v = self._range[(x, z)] = self.pf.height_range(x, z, FOOTPRINT_R)
        return v

    def rough(self, x, z):
        lo, hi = self.height_range(x, z)
        return hi - lo

    def hump(self, a, b):
        """How far the terrain between a and b rises above the higher end (exact)."""
        hs = self.pf.segment_heights(a, b)
        return max(hs) - max(hs[0], hs[-1])

    def step_grade(self, a, b):
        """The steepest climb or drop over any 200-unit stretch between a and b (sampled
        every ~100 units)."""
        n = max(2, int(dist(a, b) / 100))
        y = self.pf.terrain_y
        hs = [y(a[0] + (b[0] - a[0]) * i / n, a[1] + (b[1] - a[1]) * i / n) for i in range(n + 1)]
        step = dist(a, b) / n
        k = max(1, int(round(200 / step))) if step > 0 else 1
        return max([abs(hs[i + k] - hs[i]) / (k * step) for i in range(len(hs) - k)] or [0.0])

    def on_map(self, x, z):
        """Inside the playfield's MAP_EDGE margin and on a textured supertile."""
        pf = self.pf
        return MAP_EDGE <= x <= pf.unitW - MAP_EDGE and MAP_EDGE <= z <= pf.unitD - MAP_EDGE and pf.supertile_id(x, z) > 0

    def on_terrain(self, x, z):
        """on_map for the whole footprint around (x, z)."""
        pf = self.pf
        if not (MAP_EDGE <= x <= pf.unitW - MAP_EDGE and MAP_EDGE <= z <= pf.unitD - MAP_EDGE):
            return False
        return all(pf.supertile_id(x + dx, z + dz) > 0 for dx, dz in _FOOTPRINT_CORNERS)

    def ahead_problem(self, x, z, rot16, run, ctf, drops=True):
        """Why the first run units ahead of a car at (x, z) are not drivable, or None. drops:
        also reject a cliff edge (not for submarines, which float over it)."""
        f = heading(rot16)
        if self.fence_between((x, z), (x + f[0] * run, z + f[1] * run)):
            return 'fence ahead'
        stretch = 200
        hs = []
        for d in range(0, run + stretch + 1, 50):
            px, pz = x + f[0] * d, z + f[1] * d
            if d <= run:
                if not self.on_map(px, pz):
                    return 'map edge %d ahead' % d
                hit = self.obstacle_at(px, pz, ctf, slack=300) if d >= 300 else None
                if hit:
                    return '%s %d ahead' % (hit, d)
            hs.append(self.pf.terrain_y(px, pz))
        k = stretch // 50
        for i in range(len(hs) - k):
            grade = (hs[i + k] - hs[i]) / stretch
            if grade > MAX_STEP_GRADE:
                return 'wall %d ahead (%d%% over %d)' % (i * 50, 100 * grade, stretch)
            if drops and grade < -MAX_DROP_GRADE:
                return 'drop %d ahead (%d%% over %d)' % (i * 50, -100 * grade, stretch)
        return None


# ---- the constraint check ---------------------------------------------------------------------

class Context:
    """What a slot is checked against: its map, set, and the set's authored slots."""

    def __init__(self, md, set_name, authored):
        self.md, self.set, self.authored = md, set_name, authored
        self.fwd = heading(authored[0].rot16)
        self.cx = sum(s.x for s in authored) / len(authored)
        self.cz = sum(s.z for s in authored) / len(authored)
        if set_name == SET_RACE:
            self.rear = min(self.depth(s.pos) for s in authored)
            self.p0_gap = self.depth(authored[0].pos) - self.rear
        if set_name == SET_BATTLE:
            self.ring = min(dist(s.pos, (self.cx, self.cz)) for s in authored)
        if set_name == SET_CTF:
            b0, b1 = md.bases[0], md.bases[1]
            self.bisector = ((b0[0] + b1[0]) / 2, (b0[1] + b1[1]) / 2)
            n = (b0[0] - b1[0], b0[1] - b1[1])
            ln = dist(b0, b1)
            self.to_red = (n[0] / ln, n[1] / ln)       # unit normal pointing at the red (0) base
        self._memo = {}

    def depth(self, p):
        return p[0] * self.fwd[0] + p[1] * self.fwd[1]

    def side(self, slot):
        """Signed distance into the slot's own team half (CTF)."""
        d = (slot.x - self.bisector[0]) * self.to_red[0] + (slot.z - self.bisector[1]) * self.to_red[1]
        return d if slot.team == 0 else -d

    def source_for(self, slot):
        """The authored slot a generated one is checked against."""
        if self.set == SET_RACE:
            return self.authored[slot.player - AUTHORED]
        group = [a for a in self.authored if self.set != SET_CTF or a.team == slot.team]
        return min(group, key=lambda a: (dist(slot.pos, a.pos), a.player))

    def preferred_heading(self, slot):
        if self.set == SET_RACE:
            return self.authored[slot.player - AUTHORED].rot16
        if self.set == SET_CTF:
            return self.source_for(slot).rot16         # every team's authored slots share one heading
        return rot16_towards(self.cx - slot.x, self.cz - slot.z)


def check_site(ctx, slot, full=False):
    """The checks that depend only on the slot itself. Returns (failures, metrics). Search mode
    stops at the first failure and is memoized; full mode runs every check (reports, tests)."""
    key = (slot.player, slot.x, slot.z, slot.rot16)
    if not full and key in ctx._memo:
        return ctx._memo[key]
    md = ctx.md
    fails, m = [], {}
    p = slot.pos
    src = slot.source
    sub = md.name in SUBMARINE_MAPS

    def failed(msg):
        fails.append(msg)
        return not full

    while True:
        if not md.on_terrain(*p) and failed('off the map or on a blank supertile'):
            break
        if ctx.set == SET_RACE:
            m['behind'] = ctx.rear - ctx.depth(p)
            if m['behind'] < RACE_BEHIND and failed('only %d behind the authored grid' % m['behind']):
                break
            if slot.rot16 != src.rot16 and failed('heading differs from the grid'):
                break
        if ctx.set == SET_BATTLE:
            m['radius'] = dist(p, (ctx.cx, ctx.cz))
            if m['radius'] < ctx.ring and failed('inside the authored ring (%d from its centre, ring %d)' % (m['radius'], ctx.ring)):
                break
        if ctx.set == SET_CTF:
            m['side'] = ctx.side(slot)
            if m['side'] < CTF_SIDE_MARGIN and failed('%d units into its own half (need %d)' % (m['side'], CTF_SIDE_MARGIN)):
                break
        m['obstacle'] = md.obstacle_at(slot.x, slot.z, ctx.set == SET_CTF)
        if m['obstacle'] and failed('inside the clearance of a ' + m['obstacle']):
            break
        m['fence_dist'] = md.fence_distance(p, 4 * FENCE_CLEARANCE)
        if m['fence_dist'] < FENCE_CLEARANCE and failed('only %d from a fence' % m['fence_dist']):
            break
        m['liquid'] = md.liquid_at(*p)
        if m['liquid'] and failed('in ' + m['liquid']):
            break
        m['fence_src'] = md.fence_between(p, src.pos)
        if m['fence_src'] and failed('fence between it and its source p%d' % src.player):
            break
        if ctx.set == SET_RACE:
            n = len(md.pf.checkpoints)
            m['checkpoints'] = md.checkpoints_between(p, src.pos)
            bad = [c for c in m['checkpoints'] if c != n - 1]
            if bad and failed('behind checkpoint %d (only N-1 = %d is allowed)' % (bad[0], n - 1)):
                break
        m['rough'] = md.rough(*p)
        if m['rough'] > MAX_ROUGH and not sub and failed('footprint height range %d > %d' % (m['rough'], MAX_ROUGH)):
            break
        m['dh'] = md.pf.terrain_y(*p) - md.pf.terrain_y(*src.pos)
        m['dist'] = dist(p, src.pos)
        limit = MAX_DH + MAX_GRADE * m['dist']
        if abs(m['dh']) > limit and not sub and failed('height %+d from its source > %d' % (m['dh'], limit)):
            break
        m['hump'] = md.hump(p, src.pos)
        if m['hump'] > MAX_HUMP and failed('terrain rises %d between it and its source' % m['hump']):
            break
        m['step'] = md.step_grade(p, src.pos)
        if m['step'] > MAX_STEP_GRADE and not sub and \
                failed('ground between it and its source is %d%% steep' % (100 * m['step'])):
            break
        m['ahead'] = md.ahead_problem(slot.x, slot.z, slot.rot16, CLEAR_RUN[ctx.set], ctx.set == SET_CTF, drops=not sub)
        if m['ahead']:
            failed(m['ahead'])
        break
    if not full:
        ctx._memo[key] = (fails, m)
    return fails, m


def check_slot(ctx, slot, others, full=False):
    """Check one generated slot against every constraint. others: every other slot of the set
    (authored + generated). Returns (failures, metrics); see check_site for full."""
    m = {}
    p = slot.pos
    nearest = min(others, key=lambda o: (dist(p, o.pos), o.player))
    m['spacing'] = dist(p, nearest.pos)
    fails = []
    if m['spacing'] < MIN_SPACING:
        fails.append('only %d from p%d' % (m['spacing'], nearest.player))
        if not full:
            return fails, m
    site_fails, site = check_site(ctx, slot, full)
    m.update(site)
    fails += site_fails
    if fails and not full:
        return fails, m
    m['fence_nbr'] = ctx.md.fence_between(p, nearest.pos)
    if m['fence_nbr']:
        fails.append('fence between it and its neighbour p%d' % nearest.player)
        if not full:
            return fails, m
    m['path'] = path_conflict(slot, others)
    if m['path']:
        fails.append(m['path'])
    return fails, m


def on_nose(a, b):
    """True if car b starts on car a's nose: in a's path (PATH_RANGE ahead, or HEADON_RANGE when
    they face each other, and PATH_WIDTH to either side), unless both face the same way."""
    turn = abs(turn16(a.rot16, b.rot16))
    if turn <= 2:
        return False
    f = heading(a.rot16)
    dx, dz = b.x - a.x, b.z - a.z
    ahead = dx * f[0] + dz * f[1]
    side = dz * f[0] - dx * f[1]
    return 0 < ahead <= (HEADON_RANGE if turn >= 7 else PATH_RANGE) and abs(side) <= PATH_WIDTH


def path_conflict(slot, others):
    """Why slot and another car would drive into each other at the start, or None."""
    for o in sorted(others, key=lambda o: o.player):
        if on_nose(o, slot):
            return 'on the nose of p%d' % o.player
        if on_nose(slot, o):
            return 'p%d is on its nose' % o.player
    return None


def set_problems(ctx, extra):
    """Checks on a finished set as a whole."""
    problems = []
    everyone = ctx.authored + extra
    if len({o.pos for o in everyone}) != len(everyone):
        problems.append('duplicate positions')
    if ctx.set == SET_RACE:
        back = min(ctx.depth(s.pos) for s in extra)
        gap = ctx.depth(extra[0].pos) - back
        if gap > ctx.p0_gap + ROW_TOL:
            problems.append('slot 6 is %d from the back of its wave (player 0: %d)' % (gap, ctx.p0_gap))
    if ctx.set == SET_CTF and any(s.team != (s.player & 1) for s in extra):
        problems.append('wrong team')
    return problems


# ---- generation --------------------------------------------------------------------------------

def rule_source(set_name, n, p):
    """The authored slot the rule derives player p's slot from, and its wave (StartSlots.c
    RuleSource): slot p % n on the grid and the ring; in CTF a teammate, copied in turn."""
    if set_name == SET_CTF:
        team = p & 1
        members = (n - team + 1) // 2
        k = p // 2
        return team + 2 * (k % members), k // members
    return p % n, p // n


RULE_PUSH_STEP, RULE_PUSH_MAX = 100, 300


def rule_coord(v, size):
    """StartSlots.c RuleCoord: truncate toward zero, kept on the playfield."""
    return 0 if v < 0 else size - 1 if v > size - 1 else math.trunc(v)


def rule_slots(ctx):
    """The procedural rule (StartSlots.c, from the 12-player prototype) for players 6-11:
    [(player, x, z, heading in radians, source slot)]. Like PlaceRuleSlot, a slot within
    MIN_SPACING of one placed before it moves on in its wave's direction."""
    a = ctx.authored
    n = len(a)
    placed = [s.pos for s in a]
    w, d = ctx.md.pf.unitW, ctx.md.pf.unitD

    def place(x, z, ux, uz):
        for k in range(RULE_PUSH_MAX + 1):
            px, pz = rule_coord(x + ux * (RULE_PUSH_STEP * k), w), rule_coord(z + uz * (RULE_PUSH_STEP * k), d)
            if all((px - qx) ** 2 + (pz - qz) ** 2 >= MIN_SPACING ** 2 for qx, qz in placed):
                break
        placed.append((px, pz))
        return px, pz

    th = 2 * math.pi / (2 * n)                          # battle ring: half a slot
    cos_th, sin_th = (math.sqrt(3) / 2, 0.5) if n == 6 else (math.cos(th), math.sin(th))
    step = {}
    if ctx.set == SET_CTF:
        for t in (0, 1):                                # each team's wave step
            members = (n - t + 1) // 2
            if members == 1:
                step[t] = heading(a[t].rot16) + (RACE_GAP,)
                continue
            u, v = a[t + 2 * (members - 2)], a[t + 2 * (members - 1)]
            sx, sz = v.x - u.x, v.z - u.z
            ln = max(1.0, dist(v.pos, u.pos))
            px, pz = -sz / ln, sx / ln
            if (ctx.cx - a[t].x) * px + (ctx.cz - a[t].z) * pz < 0:
                px, pz = -px, -pz
            step[t] = (px, pz, ln)
    out = []
    for p in range(n, AUTHORED + EXTRA):
        i, wave = rule_source(ctx.set, n, p)
        src = a[i]
        if ctx.set == SET_RACE:
            f = ctx.fwd
            d = [ctx.depth(s.pos) for s in a]
            shift = max(d) - min(d) + RACE_GAP
            x, z = place(src.x - f[0] * shift * wave, src.z - f[1] * shift * wave, -f[0], -f[1])
            rot = 2 * math.pi * src.rot16 / 16
        elif ctx.set == SET_CTF:
            px, pz, ln = step[p & 1]
            x, z = place(src.x + px * ln * wave, src.z + pz * ln * wave, px, pz)
            rot = 2 * math.pi * src.rot16 / 16
        else:
            dx, dz = src.x - ctx.cx, src.z - ctx.cz
            r = dist((dx, dz), (0, 0))
            k = (r + RACE_GAP * wave) / r if r > 1 else 1
            dx, dz = dx * k, dz * k
            ox, oz = dx * cos_th + dz * sin_th, dz * cos_th - dx * sin_th
            ro = dist((ox, oz), (0, 0))
            f = heading(src.rot16)
            x, z = place(ctx.cx + ox, ctx.cz + oz, *((ox / ro, oz / ro) if ro > 1 else (-f[0], -f[1])))
            rot = 2 * math.pi * src.rot16 / 16 + th
        out.append((p, x, z, rot, src))
    return out


def generate_race(ctx):
    a = ctx.authored
    f = ctx.fwd
    lat = (-f[1], f[0])
    depths = [ctx.depth(s.pos) for s in a]
    shift = max(depths) - min(depths) + RACE_GAP
    front = max(depths)
    lat0 = sum(s.x * lat[0] + s.z * lat[1] for s in a) / len(a)

    def place(ds, dl, squeeze, stagger):
        """The rule's wave moved ds further back and dl to the right, its lanes spread by
        squeeze and each slot moved stagger units right per unit its row is further back
        (so the wave can follow a road that bends away behind the grid)."""
        slots = []
        for k, s in enumerate(a):
            off = (s.x * lat[0] + s.z * lat[1] - lat0) * (squeeze - 1.0) + dl + stagger * (front - depths[k])
            x = s.x - f[0] * (shift + ds) + lat[0] * off
            z = s.z - f[1] * (shift + ds) + lat[1] * off
            slots.append(Slot(AUTHORED + k, math.floor(x + 0.5), math.floor(z + 0.5), s.rot16, source=s))
        return slots

    def describe(ds, dl, squeeze, stagger):
        bits = []
        if ds:
            bits.append('%d further back' % ds)
        if dl:
            bits.append('%d %s' % (abs(dl), 'right' if dl > 0 else 'left'))
        if squeeze != 1.0:
            bits.append('lanes x%.2f' % squeeze)
        if stagger:
            bits.append('rows %d %s per 1000 back' % (abs(stagger) * 1000, 'right' if stagger > 0 else 'left'))
        return 'rule' if not bits else 'rule wave ' + ', '.join(bits)

    def cost(t):
        ds, dl, sq, st = t
        return (ds + abs(dl) + 4000 * abs(1 - sq) + 2000 * abs(st), ds, abs(dl), dl, sq, abs(st), st)

    squeezes = (1.0, 0.85, 0.7, 1.15)
    staggers = (0.0, -0.25, 0.25, -0.5, 0.5, -0.75, 0.75, -1.0, 1.0)
    transforms = sorted(((ds, dl, sq, st) for ds in range(0, 4001, 100) for dl in range(-5000, 5001, 100)
                         for sq in squeezes for st in staggers), key=cost)
    # The passing wave with the lowest cost + roughness penalty (a flat copy of the rule wins
    # outright; a wave on rough ground only wins if nothing flatter is nearly as cheap).
    found, found_score = None, float('inf')
    order = list(range(len(a)))
    for t in transforms:
        if cost(t)[0] >= found_score:
            break
        slots = place(*t)
        rough = 0.0
        for i in order:
            fails, m = check_slot(ctx, slots[i], ctx.authored + slots[:i] + slots[i + 1:])
            if fails:
                order.remove(i)                 # check the slot that failed last first next time
                order.insert(0, i)
                break
            rough += penalty(m, rough_only=True, md=ctx.md)
        else:
            if not set_problems(ctx, slots) and cost(t)[0] + rough < found_score:
                found, found_score = (t, slots), cost(t)[0] + rough
    if found:
        t, slots = found
        for s in slots:
            s.note = describe(*t)
        return slots

    # No wave fits as a whole: take the one with the fewest failing slots (on a coarser grid of
    # transforms), and move each failing slot to the best spot nearby, rear slots first.
    def count_failing(t):
        slots = place(*t)
        return sum(1 for i, s in enumerate(slots) if check_slot(ctx, s, ctx.authored + slots[:i] + slots[i + 1:])[0])
    coarse = [t for t in transforms if t[0] % 500 == 0 and t[1] % 500 == 0 and t[3] % 0.5 == 0]
    best = min(coarse, key=lambda t: (count_failing(t), cost(t)))
    slots = place(*best)
    for s in slots:
        s.note = describe(*best)
    order = sorted(range(len(slots)), key=lambda i: (ctx.depth(slots[i].pos), i))
    for i in order:
        others = ctx.authored + slots[:i] + slots[i + 1:]
        if not check_slot(ctx, slots[i], others)[0]:
            continue

        def keeps_rows(s, i=i):
            return not set_problems(ctx, slots[:i] + [s] + slots[i + 1:])
        spot = nearest_valid(ctx, slots[i], others, lambda x, z, r=slots[i].rot16: r, accept=keeps_rows)
        if spot is None:
            raise SystemExit('%s: no valid spot for race slot %d' % (ctx.md.name, slots[i].player))
        spot.note = 'moved %d from the %s' % (dist(spot.pos, slots[i].pos), 'rule' if best == (0, 0, 1.0, 0.0) else 'shifted wave')
        slots[i] = spot
    return slots


def penalty(m, turn=0, rough_only=False, md=None):
    rough = 0 if md is not None and md.name in SUBMARINE_MAPS else m.get('rough', 0)
    p = ROUGH_PENALTY * max(0, rough - COMFORT_ROUGH)
    if rough_only:
        return p
    return p + SPACING_PENALTY * max(0, COMFORT_SPACING - m['spacing']) + TURN_PENALTY * abs(turn)


_OFFSETS = {}


def nearest_valid(ctx, target, others, preferred, accept=None):
    """The best spot for a slot that has to move from target: the passing candidate (spacing
    against others, every check, and accept(slot)) with the lowest distance + penalty(), or None.
    Headings are tried from preferred(x, z) outwards."""
    step = SEARCH_STEP
    tx = int(round(target.x / step)) * step
    tz = int(round(target.z / step)) * step
    if step not in _OFFSETS:
        n = SEARCH_RADIUS // step + 1
        _OFFSETS[step] = [(i * step, j * step) for i in range(-n, n + 1) for j in range(-n, n + 1)]
    cands = sorted((dist((tx + dx, tz + dz), target.pos), tx + dx, tz + dz) for dx, dz in _OFFSETS[step])
    best, best_score = None, float('inf')
    for d, x, z in cands:
        if d > SEARCH_RADIUS or d >= best_score:
            break
        if min(dist((x, z), o.pos) for o in others) < MIN_SPACING:
            continue
        want = preferred(x, z)
        for rot in heading_order(want):
            turn = turn16(want, rot)
            if d + TURN_PENALTY * abs(turn) >= best_score:
                break
            s = Slot(target.player, x, z, rot)
            s.source = target.source if ctx.set == SET_RACE else ctx.source_for(s)
            fails, m = check_slot(ctx, s, others)
            if fails or (accept is not None and not accept(s)):
                continue
            score = d + penalty(m, turn, md=ctx.md)
            if score < best_score:
                best, best_score = s, score
            break
    return best


def heading_order(rot16):
    """A heading first, then turning away from it one 1/16 step at a time."""
    order = [rot16 & 15]
    for k in range(1, 9):
        order += [(rot16 + k) & 15, (rot16 - k) & 15]
    return list(dict.fromkeys(order))


def generate_arena(ctx):
    placed = []
    for p, x, z, _, _ in rule_slots(ctx):
        others = ctx.authored + placed
        target = Slot(p, x, z, 0)
        target.rot16 = ctx.preferred_heading(target)
        target.source = ctx.source_for(target)
        if not check_slot(ctx, target, others)[0]:
            target.note = 'rule'                        # the rule's spot passes: keep it
            placed.append(target)
            continue

        def preferred(cx, cz, p=p):
            return ctx.preferred_heading(Slot(p, cx, cz, 0))
        spot = nearest_valid(ctx, target, others, preferred)
        if spot is None:
            raise SystemExit('%s %s: no valid spot for player %d' % (ctx.md.name, ctx.set, p))
        spot.note = 'moved %d from the rule' % dist(spot.pos, target.pos)
        want = preferred(spot.x, spot.z)
        if spot.rot16 != want:                      # say why the preferred heading didn't do
            straight = Slot(p, spot.x, spot.z, want)
            straight.source = ctx.source_for(straight)
            spot.note += ', turned %+d/16 (straight: %s)' % (turn16(want, spot.rot16), check_slot(ctx, straight, others)[0][0])
        placed.append(spot)
    return placed


def turn16(a, b):
    d = (b - a) & 15
    return d - 16 if d > 8 else d


def generate(md, set_name):
    authored = md.authored(set_name == SET_CTF)
    if [s.player for s in authored] != list(range(AUTHORED)):
        raise SystemExit('%s %s: expected authored players 0-%d' % (md.name, set_name, AUTHORED - 1))
    ctx = Context(md, set_name, authored)
    extra = generate_race(ctx) if set_name == SET_RACE else generate_arena(ctx)
    problems = [p for s, fails, _ in validate_set(ctx, extra) for p in fails]
    if problems:
        raise SystemExit('%s %s: generated set fails: %s' % (md.name, set_name, '; '.join(problems)))
    return ctx, extra


def generate_all(terrain_dir=TERRAIN_DIR, maps=None):
    out = []
    for name in RACE_MAPS + ARENA_MAPS:
        if maps and name not in maps:
            continue
        md = MapData(name, terrain_dir)
        for set_name in md.sets():
            out.append(generate(md, set_name))
    return out


# ---- validation of a finished set --------------------------------------------------------------

def validate_set(ctx, extra):
    """Full check of one set. Returns [(slot or None, failures, metrics)]."""
    results = []
    everyone = ctx.authored + extra
    for s in extra:
        s.source = ctx.source_for(s)
        fails, m = check_slot(ctx, s, [o for o in everyone if o is not s], full=True)
        results.append((s, fails, m))
    problems = set_problems(ctx, extra)
    if problems:
        results.append((None, problems, {}))
    return results


# ---- C table -----------------------------------------------------------------------------------

HEADER = """// GENERATED by tools/gen_start_slots.py -- do not edit.
//
// Explicit start slots for players 6-11 on the shipped playfields: one entry per map and slot
// set (race grid; battle ring for Tag and Survival; Capture the Flag, even players red, odd
// green). Each entry is keyed by the map size and the exact authored MyStartCoord items for
// players 0-5 of its set, so StartSlots_Fill (StartSlots.c) falls back to the procedural rule
// on modified maps. Coordinates are world units (gPlayerInfo startX/startZ); headings are 1/16
// turns, like MyStartCoord parm[1]. The comments say how each slot was placed.
//
// Regenerate: python3 tools/gen_start_slots.py
// Verify:     python3 tools/gen_start_slots.py --check

#include "startslots.h"

const StartSlotTableEntry kStartSlotTable[] =
{
"""

FOOTER = """};

const int kNumStartSlotTableEntries = (int) (sizeof(kStartSlotTable) / sizeof(kStartSlotTable[0]));
"""


def format_table(sets):
    lines = [HEADER]
    for ctx, extra in sets:
        md = ctx.md
        lines.append('\t{\n')
        lines.append('\t\t.map = "%s", .set = %s,\n' % (md.name, SET_ENUM[ctx.set]))
        lines.append('\t\t.mapUnitWidth = %d, .mapUnitDepth = %d,\n' % (md.pf.unitW, md.pf.unitD))
        lines.append('\t\t.authored =\n\t\t{\n')
        for s in ctx.authored:
            lines.append('\t\t\t{%6d, %6d, %2d },\t// p%d\n' % (s.x, s.z, s.rot16, s.player))
        lines.append('\t\t},\n\t\t.extra =\n\t\t{\n')
        for s in extra:
            lines.append('\t\t\t{%6d, %6d, %2d },\t// p%d: %s\n' % (s.x, s.z, s.rot16, s.player, s.note))
        lines.append('\t\t},\n\t},\n')
    lines.append(FOOTER)
    return ''.join(lines)


def parse_table(text):
    """Parse a StartSlotTable.c into [(map, set, [(x, z, rot16)] * 6, [(x, z, rot16)] * 6)]."""
    entries = []
    rev = {v: k for k, v in SET_ENUM.items()}
    for name, set_enum, body in re.findall(r'\{\s*\.map = "(\w+)", \.set = (\w+),(.*?)\n\t\},', text, re.S):
        body = re.sub(r'//[^\n]*', '', body)
        coords = [tuple(int(v) for v in c) for c in re.findall(r'\{\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\s*\}', body)]
        if len(coords) != AUTHORED + EXTRA:
            raise ValueError('%s: expected %d slots, found %d' % (name, AUTHORED + EXTRA, len(coords)))
        if set_enum not in rev:
            raise ValueError('%s: unknown set %s' % (name, set_enum))
        entries.append((name, rev[set_enum], coords[:AUTHORED], coords[AUTHORED:]))
    return entries


def check_entries(entries, terrain_dir=TERRAIN_DIR, maps=None):
    """Validate parsed table entries against the map data (only the named maps, if given).
    Returns a list of problem strings."""
    problems = []
    loaded = {}
    for name, set_name, authored, extra in entries:
        if maps and name not in maps:
            continue
        md = loaded.get(name) or loaded.setdefault(name, MapData(name, terrain_dir))
        auth = md.authored(set_name == SET_CTF)
        if [(s.x, s.z, s.rot16) for s in auth] != [tuple(a) for a in authored]:
            problems.append('%s %s: authored fingerprint does not match the map' % (name, set_name))
            continue
        ctx = Context(md, set_name, auth)
        slots = []
        for i, (x, z, r) in enumerate(extra):
            if not 0 <= r < 16:
                problems.append('%s %s p%d: heading %d is not 0-15' % (name, set_name, AUTHORED + i, r))
            slots.append(Slot(AUTHORED + i, x, z, r))
        for s, fails, _ in validate_set(ctx, slots):
            for f in fails:
                problems.append('%s %s %s: %s' % (name, set_name, 'p%d' % s.player if s else 'set', f))
    return problems


def check_table(text, terrain_dir=TERRAIN_DIR, maps=None):
    """Validate a table's text: the expected entries, each checked against the map data."""
    problems = []
    entries = parse_table(text)
    expected = [(n, SET_RACE) for n in RACE_MAPS] + [(n, s) for n in ARENA_MAPS for s in (SET_BATTLE, SET_CTF)]
    if [(e[0], e[1]) for e in entries] != expected:
        problems.append('table entries %s != expected %s' % ([(e[0], e[1]) for e in entries], expected))
    return problems + check_entries(entries, terrain_dir, maps)


# ---- report ------------------------------------------------------------------------------------

def fence_text(d):
    return '>=%d' % d if d >= 4 * FENCE_CLEARANCE else '%d' % d


def summary(ctx, results):
    """One line of worst-case metrics for a validated set."""
    ms = [m for s, _, m in results if s is not None]
    bits = ['min gap %d' % min(m['spacing'] for m in ms),
            'min fence %s' % fence_text(min(m.get('fence_dist', 0) for m in ms))]
    if ctx.md.name not in SUBMARINE_MAPS:
        bits += ['max rough %d' % max(m.get('rough', 0) for m in ms),
                 'max |dh| %d' % max(abs(m.get('dh', 0)) for m in ms),
                 'max step %.2f' % max(m.get('step', 0) for m in ms)]
    bits.append('max hump %d' % max(m.get('hump', 0) for m in ms))
    if ctx.set == SET_RACE:
        n = len(ctx.md.pf.checkpoints)
        bits.append('min behind %d' % min(m.get('behind', 0) for m in ms))
        bits.append('%d behind ckpt N-1' % sum(1 for m in ms if n - 1 in m.get('checkpoints', [])))
    if ctx.set == SET_BATTLE:
        bits.append('min radius %d (ring %d)' % (min(m.get('radius', 0) for m in ms), ctx.ring))
    if ctx.set == SET_CTF:
        bits.append('min side %d' % min(m.get('side', 0) for m in ms))
    failed = sum(1 for _, fails, _ in results if fails)
    return ', '.join(bits) + ('; %d FAILING' % failed if failed else '; all pass')


def report(sets):
    out = []
    for ctx, extra in sets:
        n = len(ctx.md.pf.checkpoints)
        results = validate_set(ctx, extra)
        notes = sorted({s.note.split(',')[0] if s.note.startswith('moved') else s.note for s in extra})
        out.append('%s %s: %s [%s]' % (ctx.md.name, ctx.set, summary(ctx, results), '; '.join(notes)))
        for s, fails, m in results:
            if s is None:
                out.append('  ' + '; '.join(fails))
                continue
            bits = ['p%-2d (%6d,%6d) rot %2d src p%d' % (s.player, s.x, s.z, s.rot16, s.source.player),
                    'gap %4d' % m.get('spacing', 0), 'rough %3d' % m.get('rough', 0),
                    'dh %+4d/%4d' % (m.get('dh', 0), m.get('dist', 0)), 'hump %2d' % m.get('hump', 0),
                    'step %.2f' % m.get('step', 0), 'fence %5s' % fence_text(m.get('fence_dist', 0))]
            if ctx.set == SET_RACE:
                bits.append('behind %5d' % m.get('behind', 0))
                if n - 1 in m.get('checkpoints', []):
                    bits.append('behind ckpt N-1')
            if ctx.set == SET_BATTLE:
                bits.append('radius %4d' % m.get('radius', 0))
            if ctx.set == SET_CTF:
                bits.append('side %5d' % m.get('side', 0))
            bits.append('OK' if not fails else 'FAIL: ' + '; '.join(fails))
            out.append('  ' + '  '.join(bits) + '  [' + s.note + ']')
    return '\n'.join(out)


# ---- previews ----------------------------------------------------------------------------------

def render_previews(sets, out_dir):
    """Before (shipped + procedural rule) / after (table) PNGs per map, plus two contact sheets."""
    import numpy as np
    from PIL import Image, ImageDraw, ImageFont
    os.makedirs(out_dir, exist_ok=True)

    def font(size):
        for path in ('/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf',
                     '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'):
            if os.path.exists(path):
                return ImageFont.truetype(path, size)
        return ImageFont.load_default()

    f_small, f_title = font(11), font(15)

    def base_image(md, x0, z0, x1, z1, px):
        pf = md.pf
        img, upp, (ox, oz) = pf.render_region(x0, z0, x1, z1)
        im = Image.fromarray(img)
        pad = Image.new('RGB', (int((x1 - x0) / upp), int((z1 - z0) / upp)))
        pad.paste(im, (int((ox - x0) / upp), int((oz - z0) / upp)))
        im = pad.resize((px, px), Image.LANCZOS)
        # hillshade so hedges, ridges and banks read in the preview
        n = 200
        step = (x1 - x0) / n
        h = np.array([[pf.terrain_y(x0 + (i + 0.5) * step, z0 + (j + 0.5) * step) for i in range(n)]
                      for j in range(n)])
        gz, gx = np.gradient(h, step)
        shade = np.clip(1.0 + 0.9 * (gx + gz) / np.sqrt(1 + gx * gx + gz * gz), 0.35, 1.4)
        shade = np.array(Image.fromarray((shade * 150).astype(np.uint8)).resize((px, px), Image.BILINEAR),
                         dtype=float) / 150
        arr = np.clip(np.array(im, dtype=float) * shade[..., None], 0, 255).astype(np.uint8)
        return Image.fromarray(arr)

    def draw(md, set_name, im, x0, z0, x1, z1, slot_groups, title):
        d = ImageDraw.Draw(im)
        sc = im.size[0] / (x1 - x0)

        def P(x, z):
            return ((x - x0) * sc, (z - z0) * sc)
        for x0s, z0s, x1s, z1s, surface, label in md.liquids:
            d.rectangle([P(x0s, z0s), P(x1s, z1s)], outline=(80, 170, 255) if label == 'water' else (20, 20, 20))
        for cx, cz, r, label, ctf_only in md.circles:
            if ctf_only and set_name != SET_CTF:
                continue
            X, Y = P(cx, cz)
            rr = r * sc
            if -rr < X < im.size[0] + rr and -rr < Y < im.size[1] + rr:
                d.ellipse([X - rr, Y - rr, X + rr, Y + rr], outline=(255, 0, 255))
        for fen in md.pf.fences:
            d.line([P(*p) for p in fen['nubs']], fill=(255, 255, 255), width=2)
        n = len(md.pf.checkpoints)
        for i, (a, b) in enumerate(md.pf.checkpoints):
            if i in (0, n - 1, n - 2):
                col = {0: (255, 230, 0), n - 1: (255, 120, 0), n - 2: (255, 60, 200)}[i]
                d.line([P(*a), P(*b)], fill=col, width=2)
        for team, b in md.bases.items():
            X, Y = P(*b)
            d.rectangle([X - 8, Y - 8, X + 8, Y + 8], outline=(255, 60, 60) if team == 0 else (60, 255, 60), width=3)
        r = 7
        for slots, style in slot_groups:
            for s, rot in slots:
                X, Y = P(s.x, s.z)
                fx, fz = terlib.heading_vector(rot)
                col = style(s)
                d.ellipse([X - r, Y - r, X + r, Y + r], fill=col, outline=(0, 0, 0))
                d.line([X, Y, X + fx * r * 2.4, Y + fz * r * 2.4], fill=(0, 0, 0), width=3)
                d.line([X, Y, X + fx * r * 2.4, Y + fz * r * 2.4], fill=col, width=1)
                d.text((X + r + 1, Y - r - 3), str(s.player + 1), fill=(255, 255, 255), font=f_small,
                       stroke_width=2, stroke_fill=(0, 0, 0))
        d.rectangle([0, 0, im.size[0], 20], fill=(0, 0, 0))
        d.text((5, 2), title, fill=(255, 255, 255), font=f_title)
        return im

    SHIPPED = (60, 140, 255)
    TEAM = [(230, 50, 50), (50, 200, 70)]
    NEW_TEAM = [(255, 150, 170), (170, 255, 150)]
    GOOD, BAD = (60, 230, 90), (255, 40, 40)

    def rot_of(s):
        return 2 * math.pi * s.rot16 / 16

    by_map = {}
    for ctx, extra in sets:
        by_map.setdefault(ctx.md.name, []).append((ctx, extra))

    def region(md, set_name, pts):
        """The world square a preview shows: the whole arena for CTF, else the slots plus room."""
        if set_name == SET_CTF:
            nz = [(r, c) for r, row in enumerate(md.pf.stGrid) for c, v in enumerate(row) if v > 0]
            r0, r1 = min(r for r, _ in nz), max(r for r, _ in nz)
            c0, c1 = min(c for _, c in nz), max(c for _, c in nz)
            su = terlib.SUPERTILE_UNITS
            span = max(c1 - c0 + 1, r1 - r0 + 1) * su
            return c0 * su, r0 * su, c0 * su + span, r0 * su + span
        mx = (min(p[0] for p in pts) + max(p[0] for p in pts)) / 2
        mz = (min(p[1] for p in pts) + max(p[1] for p in pts)) / 2
        half = max(max(p[0] for p in pts) - min(p[0] for p in pts),
                   max(p[1] for p in pts) - min(p[1] for p in pts)) / 2 + (3000 if set_name == SET_RACE else 2000)
        return mx - half, mz - half, mx + half, mz + half

    race_tiles, arena_tiles = [], []
    px = 460
    for name, entries in by_map.items():
        md = entries[0][0].md
        rows = []
        for ctx, extra in entries:                  # one row per slot set: before | after
            views = []
            for label, pick in (('before: procedural rule', 'rule'), ('after: table', 'table')):
                if pick == 'rule':
                    gen = []
                    for p, x, z, rot, src in rule_slots(ctx):
                        gen.append((Slot(p, x, z, int(round(rot / (2 * math.pi / 16)))), rot))
                    everyone = ctx.authored + [s for s, _ in gen]
                    bad = set()
                    for s, _ in gen:
                        s.source = ctx.source_for(s)
                        if ctx.set == SET_RACE:
                            s.rot16 = s.source.rot16
                        if check_slot(ctx, s, [o for o in everyone if o is not s])[0]:
                            bad.add(s.player)
                else:
                    gen = [(s, rot_of(s)) for s in extra]
                    bad = {s.player for s, fails, _ in validate_set(ctx, extra) if s is not None and fails}
                shipped = [(s, rot_of(s)) for s in ctx.authored]
                if ctx.set == SET_CTF:
                    groups = [(shipped, lambda s: TEAM[s.team]),
                              (gen, lambda s, bad=bad: BAD if s.player in bad else NEW_TEAM[s.team])]
                else:
                    groups = [(shipped, lambda s: SHIPPED),
                              (gen, lambda s, bad=bad: BAD if s.player in bad else GOOD)]
                views.append((label, groups, [(s.x, s.z) for s, _ in shipped + gen]))
            x0, z0, x1, z1 = region(md, ctx.set, views[0][2] + views[1][2])
            base = base_image(md, x0, z0, x1, z1, px)
            row = Image.new('RGB', (px * 2 + 6, px), (20, 20, 24))
            title = name.split('_')[1] + ('' if ctx.set == SET_RACE else ' ' + ctx.set)
            for i, (label, groups, _) in enumerate(views):
                im = draw(md, ctx.set, base.copy(), x0, z0, x1, z1, groups, '%s  %s' % (title, label))
                row.paste(im, (i * (px + 6), 0))
            rows.append(row)
        pair = Image.new('RGB', (px * 2 + 6, len(rows) * (px + 6) - 6), (20, 20, 24))
        for i, row in enumerate(rows):
            pair.paste(row, (0, i * (px + 6)))
        pair.save(os.path.join(out_dir, name + '.png'))
        (arena_tiles if md.is_arena else race_tiles).append(pair)

    def sheet(tiles, cols, title, legend, path):
        if not tiles:
            return
        w, h = tiles[0].size
        rows = (len(tiles) + cols - 1) // cols
        im = Image.new('RGB', (cols * (w + 8) + 8, rows * (h + 8) + 80), (20, 20, 24))
        d = ImageDraw.Draw(im)
        d.text((10, 8), title, fill=(255, 255, 255), font=font(20))
        for k, line in enumerate(legend):
            d.text((10, 36 + 14 * k), line, fill=(220, 220, 220), font=f_small)
        for i, t in enumerate(tiles):
            im.paste(t, (8 + (i % cols) * (w + 8), 72 + (i // cols) * (h + 8)))
        im.save(path)

    legend = ['left: procedural rule (the fallback for unknown maps)   right: generated table   '
              'red = slot fails a constraint',
              'yellow line = finish (ckpt 0)   orange = ckpt N-1   pink = ckpt N-2   white = fences   '
              'magenta circles = obstacle clearance   blue boxes = water patches, black = tar']
    sheet(race_tiles, 2, 'Race grids: shipped slots 1-6 (blue) + slots 7-12 (green)', legend,
          os.path.join(out_dir, 'race_grids.png'))
    sheet(arena_tiles, 2, 'Arenas: battle ring (top; blue shipped, green new) + CTF (bottom; red/green '
          'shipped, pink/light green new); squares = team bases', legend, os.path.join(out_dir, 'arenas.png'))


# ---- main --------------------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--check', action='store_true', help='verify the checked-in table instead of writing it')
    ap.add_argument('--report', action='store_true', help='print per-slot constraint metrics')
    ap.add_argument('--render', metavar='DIR', help='write before/after preview PNGs to DIR')
    ap.add_argument('--table', default=TABLE_PATH, help='table path (default: %(default)s)')
    args = ap.parse_args(argv)

    sets = generate_all()
    text = format_table(sets)
    status = 0
    if args.check:
        with open(args.table) as f:
            current = f.read()
        if current != text:
            print('%s is out of date: run python3 tools/gen_start_slots.py' % args.table)
            status = 1
        for p in check_table(current):
            print('constraint: ' + p)
            status = 1
        if status == 0:
            print('%s is up to date; all %d generated slots pass' % (args.table, EXTRA * len(sets)))
    else:
        problems = check_table(text)
        if problems:
            print('\n'.join(problems))
            return 1
        with open(args.table, 'w', newline='\n') as f:
            f.write(text)
        print('wrote %s (%d entries)' % (args.table, len(sets)))
    if args.report:
        print(report(sets))
    if args.render:
        render_previews(sets, args.render)
        print('previews in ' + args.render)
    return status


if __name__ == '__main__':
    sys.exit(main())
