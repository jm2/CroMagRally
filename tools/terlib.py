"""Reader for Cro-Mag Rally playfields (Data/Terrain/*.ter + *.ter.rsrc).

A playfield is a classic Mac file: the data fork (``.ter``) holds the LZSS-compressed
supertile textures, and the resource fork, stored beside it as an AppleDouble sidecar
(``.ter.rsrc``), holds everything else. This module parses the resources the game reads
in ``ReadDataFromPlayfieldFile`` (Source/System/File.c):

    Hedr 1000         header (item/fence/checkpoint counts, map size, tile size)
    Itms 1000         terrain items (start coords, props, POWs, water patches, ...)
    Fenc 1000 + FnNb  fences and their nubs
    CkPt 1000         checkpoints (0 is the finish line)
    YCrd 1000         vertex heights, (mapH + 1) x (mapW + 1)
    STgd 1000         supertile grid (which texture each 8x8-tile supertile uses)

All values are converted to the game's world units, the same units the game uses after
loading: item, fence and checkpoint coordinates are multiplied by MAP2UNIT_VALUE (50),
and heights by File.c's ``yScale`` (TERRAIN_POLYGON_SIZE / Hedr.tileSize, with the default
terrainHeight physics setting of 1.0).

Everything except texture decoding and rendering uses only the standard library, so
checkers that import this module run on CI without numpy. ``supertile_rgb`` and
``render_region`` import numpy when called.
"""

import array
import math
import os
import struct
import sys

TERRAIN_POLYGON_SIZE = 800.0        # world units per terrain tile (terrain.h)
OREOMAP_TILE_SIZE = 16              # map-editor pixels per tile
MAP2UNIT = TERRAIN_POLYGON_SIZE / OREOMAP_TILE_SIZE     # MAP2UNIT_VALUE = 50
SUPERTILE_SIZE = 8                  # tiles per supertile edge
SUPERTILE_TEXMAP_SIZE = 128         # texels per supertile texture edge
SUPERTILE_UNITS = SUPERTILE_SIZE * TERRAIN_POLYGON_SIZE  # 6400 world units per supertile

MAP_ITEM_MYSTARTCOORD = 0           # Itms type of a player start slot

_HEDR_FORMAT = '>4b5i3f5i'          # PlayfieldHeaderType (File.c)
_ITEM_FORMAT = '>IIH4BH'            # TerrainItemEntryType: x, y, type, parm[4] (Byte), flags
_ITEM_SIZE = struct.calcsize(_ITEM_FORMAT)


def read_appledouble_rsrc(path):
    """Return the raw resource fork stored in an AppleDouble file."""
    with open(path, 'rb') as f:
        data = f.read()
    if struct.unpack('>I', data[:4])[0] != 0x00051607:
        raise ValueError('%s: not an AppleDouble file' % path)
    num_entries = struct.unpack('>H', data[24:26])[0]
    for i in range(num_entries):
        entry_id, offset, length = struct.unpack('>III', data[26 + i * 12:38 + i * 12])
        if entry_id == 2:                                   # resource fork
            return data[offset:offset + length]
    raise ValueError('%s: no resource fork entry' % path)


def parse_rsrc(fork):
    """Parse a Mac resource fork into {(type, id): bytes}."""
    data_off, map_off, _, map_len = struct.unpack('>IIII', fork[:16])
    rmap = fork[map_off:map_off + map_len]
    type_list = rmap[struct.unpack('>H', rmap[24:26])[0]:]
    num_types = struct.unpack('>H', type_list[:2])[0] + 1
    resources = {}
    for t in range(num_types):
        rtype, count, ref_off = struct.unpack('>4sHH', type_list[2 + t * 8:10 + t * 8])
        for r in range(count + 1):
            ref = type_list[ref_off + r * 12:ref_off + r * 12 + 12]
            rid, _, attr_off = struct.unpack('>hHI', ref[:8])
            p = data_off + (attr_off & 0xFFFFFF)
            length = struct.unpack('>I', fork[p:p + 4])[0]
            resources[(rtype.decode('mac_roman'), rid)] = fork[p + 4:p + 4 + length]
    return resources


def lzss_decode(src, capacity):
    """Decode Pangea's LZSS stream (Source/System/LZSSDecode.c) into at most capacity bytes."""
    out = bytearray()
    ring = bytearray(b' ' * 4096)
    cursor = 4096 - 18
    flags = 0
    i = 0
    n = len(src)
    while i < n:
        flags >>= 1
        if (flags & 256) == 0:
            flags = src[i] | 0xff00
            i += 1
            if i == n:
                break
        if flags & 1:
            c = src[i]
            i += 1
            out.append(c)
            ring[cursor] = c
            cursor = (cursor + 1) & 4095
        else:
            if n - i < 2:
                break
            off = src[i]
            length = src[i + 1]
            i += 2
            off |= (length & 0xf0) << 4
            length = (length & 0x0f) + 3
            for k in range(length):
                c = ring[(off + k) & 4095]
                out.append(c)
                ring[cursor] = c
                cursor = (cursor + 1) & 4095
        if len(out) >= capacity:
            break
    return bytes(out[:capacity])


def seg_intersect(p1, p2, p3, p4):
    """True if segment p1-p2 intersects segment p3-p4 (endpoints included)."""
    (x1, y1), (x2, y2), (x3, y3), (x4, y4) = p1, p2, p3, p4
    d = (x2 - x1) * (y4 - y3) - (y2 - y1) * (x4 - x3)
    if abs(d) < 1e-9:
        return False
    t = ((x3 - x1) * (y4 - y3) - (y3 - y1) * (x4 - x3)) / d
    u = ((x3 - x1) * (y2 - y1) - (y3 - y1) * (x2 - x1)) / d
    return 0 <= t <= 1 and 0 <= u <= 1


def heading_vector(rot):
    """Car forward (x, z) for a rotY in radians: (-sin rotY, -cos rotY), as Checkpoints.c aims."""
    return (-math.sin(rot), -math.cos(rot))


class Playfield:
    """One parsed playfield. ``path`` is the .ter file; its .ter.rsrc must sit beside it."""

    def __init__(self, path):
        self.path = path
        self.name = os.path.basename(path)[:-len('.ter')] if path.endswith('.ter') else os.path.basename(path)
        res = parse_rsrc(read_appledouble_rsrc(path + '.rsrc'))
        self.res = res

        hdr = struct.unpack(_HEDR_FORMAT, res[('Hedr', 1000)][:struct.calcsize(_HEDR_FORMAT)])
        (self.numItems, self.mapW, self.mapH, self.numTilePages, self.numTilesInList) = hdr[4:9]
        self.tileSize, self.minY, self.maxY = hdr[9:12]
        (self.numSplines, self.numFences, self.numUniqueSuperTiles,
         self.numPaths, self.numCheckpoints) = hdr[12:17]
        self.unitW = self.mapW * TERRAIN_POLYGON_SIZE          # gTerrainUnitWidth
        self.unitD = self.mapH * TERRAIN_POLYGON_SIZE          # gTerrainUnitDepth
        self.stWide = self.mapW // SUPERTILE_SIZE
        self.stDeep = self.mapH // SUPERTILE_SIZE
        self.heightScale = TERRAIN_POLYGON_SIZE / self.tileSize  # File.c yScale

        self._read_items(res[('Itms', 1000)])
        self._read_fences(res)
        self._read_checkpoints(res)
        self._read_heights(res[('YCrd', 1000)])
        self._read_supertile_grid(res[('STgd', 1000)])
        self._tex_offsets = None

    # ---- resources ---------------------------------------------------------------------

    def _read_items(self, data):
        self.items = []
        for i in range(self.numItems):
            x, y, typ, p0, p1, p2, p3, flags = struct.unpack_from(_ITEM_FORMAT, data, i * _ITEM_SIZE)
            # File.c multiplies the uint32 coords by MAP2UNIT_VALUE in place; the result is exact.
            self.items.append(dict(x=int(x * MAP2UNIT), z=int(y * MAP2UNIT), type=typ,
                                   parm=(p0, p1, p2, p3), flags=flags))
        # FindPlayerStartCoordItems (Terrain2.c): parm[0] player, parm[1] heading in 1/16 turns,
        # parm[3] bit 0 = Capture the Flag slot.
        self.starts = [dict(player=it['parm'][0], ctf=bool(it['parm'][3] & 1), x=it['x'], z=it['z'],
                            rot16=it['parm'][1], rot=2 * math.pi * it['parm'][1] / 16.0)
                       for it in self.items if it['type'] == MAP_ITEM_MYSTARTCOORD]

    def _read_fences(self, res):
        self.fences = []
        if ('Fenc', 1000) not in res:
            return
        data = res[('Fenc', 1000)]
        for i in range(self.numFences):
            typ, num_nubs = struct.unpack_from('>Hh', data, i * 16)
            nubs_data = res[('FnNb', 1000 + i)]
            nubs = [struct.unpack_from('>ii', nubs_data, j * 8) for j in range(num_nubs)]
            self.fences.append(dict(type=typ, nubs=[(x * MAP2UNIT, z * MAP2UNIT) for x, z in nubs]))

    def fence_segments(self):
        """All fence sections as ((x0, z0), (x1, z1)) pairs."""
        segs = []
        for fence in self.fences:
            nubs = fence['nubs']
            segs.extend((nubs[i], nubs[i + 1]) for i in range(len(nubs) - 1))
        return segs

    def _read_checkpoints(self, res):
        self.checkpoints = []
        if ('CkPt', 1000) not in res:
            return
        data = res[('CkPt', 1000)]
        for i in range(self.numCheckpoints):
            _, _, x0, x1, z0, z1 = struct.unpack_from('>HH2f2f', data, i * 20)
            self.checkpoints.append(((x0 * MAP2UNIT, z0 * MAP2UNIT), (x1 * MAP2UNIT, z1 * MAP2UNIT)))

    def _read_heights(self, data):
        heights = array.array('f')
        heights.frombytes(data[:(self.mapH + 1) * (self.mapW + 1) * 4])
        if sys.byteorder == 'little':
            heights.byteswap()                              # YCrd is big-endian
        self._heights = heights
        self._rowStride = self.mapW + 1

    def _read_supertile_grid(self, data):
        # SuperTileGridType: Boolean isEmpty, pad, uint16 superTileID. ID 0 is also blank.
        self.stGrid = []
        k = 0
        for _ in range(self.stDeep):
            row = []
            for _ in range(self.stWide):
                empty, sid = struct.unpack_from('>?xH', data, k * 4)
                k += 1
                row.append(-1 if empty else sid)
            self.stGrid.append(row)

    # ---- terrain queries -----------------------------------------------------------------

    def vertex_y(self, row, col):
        """World-unit height of terrain vertex (row, col)."""
        return self._heights[row * self._rowStride + col] * self.heightScale

    def terrain_y(self, x, z):
        """Height at (x, z) exactly as GetTerrainY (Terrain.c) computes it; 0 off the map."""
        col = int(x / TERRAIN_POLYGON_SIZE) if x >= 0 else -1
        row = int(z / TERRAIN_POLYGON_SIZE) if z >= 0 else -1
        if not (0 <= col < self.mapW and 0 <= row < self.mapH):
            return 0.0
        xi = x - col * TERRAIN_POLYGON_SIZE
        zi = z - row * TERRAIN_POLYGON_SIZE
        s = TERRAIN_POLYGON_SIZE
        y0 = self.vertex_y(row, col)            # far left
        y1 = self.vertex_y(row, col + 1)        # far right
        y2 = self.vertex_y(row + 1, col + 1)    # near right
        y3 = self.vertex_y(row + 1, col)        # near left
        # CalculateSplitModeMatrix: flat tiles and |y0-y2| < |y1-y3| split "\", others "/".
        backward = (y0 == y1 == y2 == y3) or abs(y0 - y2) < abs(y1 - y3)
        if backward:
            if xi < zi:                         # triangle p0, p2, p3
                return y0 + (y2 - y3) * xi / s + (y3 - y0) * zi / s
            return y0 + (y1 - y0) * xi / s + (y2 - y1) * zi / s     # triangle p0, p1, p2
        if s - xi > zi:                         # triangle p0, p1, p3
            return y0 + (y1 - y0) * xi / s + (y3 - y0) * zi / s
        return y1 + (y2 - y3) * (xi - s) / s + (y2 - y1) * zi / s   # triangle p1, p2, p3

    def supertile_id(self, x, z):
        """Texture id of the supertile under (x, z), or -1 when off the map or blank."""
        if not (0 <= x < self.unitW and 0 <= z < self.unitD):
            return -1
        sid = self.stGrid[int(z // SUPERTILE_UNITS)][int(x // SUPERTILE_UNITS)]
        return sid if sid > 0 else -1

    # ---- textures (numpy) ------------------------------------------------------------------

    def _index_textures(self):
        with open(self.path, 'rb') as f:
            data = f.read()
        offsets = []
        p = 0
        for _ in range(self.numUniqueSuperTiles):
            size = struct.unpack('>I', data[p:p + 4])[0]
            offsets.append((p + 4, size))
            p += 4 + size
        self._data = data
        self._tex_offsets = offsets

    def supertile_rgb(self, sid):
        """Decode supertile texture ``sid`` to a 128x128x3 uint8 numpy array (needs numpy)."""
        import numpy as np
        if self._tex_offsets is None:
            self._index_textures()
        off, size = self._tex_offsets[sid]
        raw = lzss_decode(self._data[off:off + size], SUPERTILE_TEXMAP_SIZE * SUPERTILE_TEXMAP_SIZE * 2)
        px = np.frombuffer(raw, dtype='>u2').reshape(SUPERTILE_TEXMAP_SIZE, SUPERTILE_TEXMAP_SIZE)
        r = ((px >> 10) & 31) * 255 // 31
        g = ((px >> 5) & 31) * 255 // 31
        b = (px & 31) * 255 // 31
        return np.stack([r, g, b], axis=-1).astype(np.uint8)

    def render_region(self, x0, z0, x1, z1):
        """Top-down texture mosaic of the supertiles covering a world rect (needs numpy).

        Returns (rgb array, world units per pixel, (origin x, origin z)); blank supertiles are
        black. The array covers whole supertiles, so crop it with the returned origin.
        """
        import numpy as np
        c0 = int(max(0, x0 // SUPERTILE_UNITS))
        c1 = int(min(self.stWide - 1, x1 // SUPERTILE_UNITS))
        r0 = int(max(0, z0 // SUPERTILE_UNITS))
        r1 = int(min(self.stDeep - 1, z1 // SUPERTILE_UNITS))
        tex = SUPERTILE_TEXMAP_SIZE
        img = np.zeros(((r1 - r0 + 1) * tex, (c1 - c0 + 1) * tex, 3), dtype=np.uint8)
        for r in range(r0, r1 + 1):
            for c in range(c0, c1 + 1):
                sid = self.stGrid[r][c]
                if sid <= 0:
                    continue
                img[(r - r0) * tex:(r - r0 + 1) * tex, (c - c0) * tex:(c - c0 + 1) * tex] = self.supertile_rgb(sid)
        return img, SUPERTILE_UNITS / tex, (c0 * SUPERTILE_UNITS, r0 * SUPERTILE_UNITS)
