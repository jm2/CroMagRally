# feat/12-players: work-in-progress status (2026-09-22)

This branch implements charter §4 (12 players) on top of `fix/player-limit-gaps`
(PR #42, merged to master as `c92e559`). Work stopped part-way when the session's
usage quota ran low. **`MAX_PLAYERS` is now 12** (commit "Raise the player limit to
twelve"); every earlier change is written for any `MAX_PLAYERS` and is a no-op, or
bit-identical, at 6 cars. Delete this file (and `docs/wip/`) before merging.

The protocol cookie stays `CMR8`: it is unreleased (v3.1.x ship `CMR7`), so
branch 2 doesn't bump it (owner decision, 2026-09-22).

## Done (in commit order)

- **Places 7th–12th** (§4.3):
  - Ordinal suffixes for every place in every language.
  - Places past 6th drawn with `wallfont` digits (existing art). Places 1–6
    keep their sprites; the draw log is identical at 6 players.
  - The announcer stays silent below 6th.
  - Branch 1's `InfobarPlace.c` asserts are replaced by a generic guard.
- **Distinct drivers** (§4.4):
  - All 12 sex/outfit combinations are used (`DriverLooks.c`).
  - `CycleSkin` swaps stay distinct and can no longer spin forever.
  - The minimap marks the second wearer of each outfit with a centre dot. This
    was chosen over six new colours for colour-blind contrast; screenshots are in
    `/var/tmp/cmr-b2-drivers-shots/`.
- **Budgets** (§4.6–4.7), all scaling with `MAX_PLAYERS` and giving exactly the
  old values at 6 players:
  - sound channels: 20 per 6 players, so 40 at 12;
  - skid marks: 10 per player;
  - particle groups: 40 + 5 per player, with scaled snow and bubble reserves;
  - collision list: kept at 60 and asserted;
  - a note that the supertile pool scales with panes, not cars;
  - the scoreboard drops records whose place is past the build's limit
    (documented and tested).
- **Audit fixes:**
  - The host-control validation test now builds the full event list from every
    network player.
  - Snow is only made around this machine's cameras (a LAN snow bug for high
    player numbers).
- **Tuning** (§4.5), bit-identical at ≤ 6 cars:
  - Per-place catch-up steps scale by `5/(cars-1)`.
  - POW respawn time scales by `6/cars`.
  - Tournament `placeToWin` is unchanged (owner decision below).
- **Start slots for all 17 maps** (§4.2):
  - `tools/terlib.py` playfield reader.
  - `tools/gen_start_slots.py`, a checked-in generator, and a generated table,
    `Source/Terrain/StartSlotTable.c`, with explicit slots 7–12 for the race
    grid, battle ring and CTF team sets.
  - Placement lives in `Source/Terrain/StartSlots.c`: table lookup by
    authored-slot fingerprint, a procedural fallback for unknown maps, and the
    humans-to-back swap.
  - Tests: `start_slots` (C) and `start_slot_table` (Python re-validation of the
    table against the map data).
  - Two adversarial verification rounds were run. The last one found no blockers
    in the 150 generated slots.
  - Previews (for a PR description, not the repo) are in `/var/tmp/cmr-slot-previews/`.
- **Smoke-only soak flags** (§6; two self-contained, droppable commits at the tip):
  - Flags: `--smoke-autopilot`, `--smoke-cars`, `--smoke-fixed-fps`,
    `--smoke-seed`, `--smoke-until-finish`, `--smoke-metrics`.
  - Scripts: `tools/run_race_metrics.sh` and `tools/analyze_race_metrics.py`.
  - Output matches the old env-var harness exactly: 786 of 786 values.

Each unit passed the CI-equivalent run (normal build + ctest, `-DSANITIZE=ON`
build + ctest, `StartupSmokeTests.py`, `MalformedAssetTests.py`) on its own branch.
The integrated branch passes it too: 27/27 ctest normal and sanitizer, 101
StartupSmoke PASS lines, MalformedAssetTests PASS. GitHub CI has not run on it yet.

## Not done (charter §4)

1. ~~§4.1 constants and wire~~ **done**: `MAX_PLAYERS` 12 (`MAX_CLIENTS` follows),
   `kNSpMaxPayloadLength` 1024, `NET_MAX_PENDING_EVENTS` 12, an exact-size assert
   for the 628 B host control message, an 80 KB send ring (~2 s of host messages at
   60 fps), and updated README and netcode notes. No 4CC change. CI-equivalent run:
   27/27 ctest normal and sanitizer, 101 StartupSmoke PASS lines, MalformedAssetTests PASS.
2. **CPU strandings on fences (known issue, deferred by the owner on 2026-09-22).** After a
   jump or a hard knock, a CPU occasionally lands on or behind a fence. Path-following then
   steers it into the fence forever, and the 1 s displacement stuck check either never fires
   (the car slides back and forth) or reversing can't free it (the car is wedged).
   - Jungle, 12 cars, 60 Hz, seed 12345: CPU 11 lands on the fence near checkpoint 4 at
     ≈(41500, 3390, 80800) after a 7000 u/s jump, and stays there 246 s of 280.
   - Scandinavia, prototype measurement build: the autopilot car ends up on the ridge south
     of the checkpoint-44 fence at ≈(90300, 3800, 30800). It didn't recur on this branch.

   Renders and a per-car trace patch are in `/var/tmp/cmr-b2-stuck-evidence/`. The robust
   fix is a deterministic CPU rescue that returns the car to its last checkpoint after about
   20 s without progress. That is a visible gameplay change, left for a later decision.
3. **§4.8 LAN at 12 humans:** a 12-human race runs clean on loopback (item 4). The
   bandwidth figures are computed, not measured: the host sends one 628 B control message
   per client per frame, which is ≈3.3 Mbit/s and ≈660 packets/s each way with 11 clients at
   60 fps, or ≈2.4× that at 144 Hz. Not measured on a real LAN: the host's input grace
   wait (`SDL_Delay(1)` when a client queue is empty) with 11 clients on Wi-Fi.
4. **§4.9 acceptance, partly done.** A soak on this branch (normal build, `tools/run_race_metrics.sh`)
   ran 9 tracks × 50/60/72 Hz at 12 and at 6 cars, with autopilot and seed 12345. All 54 races
   finished; logs are in `/var/tmp/cmr-soak/b2real`.
   - 6 cars: 0 stranded CPUs, and the sampled rows are identical to branch 1.
   - 12 cars: 1 stranded CPU in 27 races (Jungle 60 Hz, item 2 above).
   - 12 vs 6 cars: CPU lap 2 is +0.3%, pickups per car +0.9% (the kit's
     prototype measured −11% before POW scaling), time to checkpoint 5 +8% (prototype
     +26%), and hard hits per minute ×2.3.
   - Narrow China is still +14% on lap 2.

   - 12 cars under ASan/UBSan (`-DSANITIZE=ON`, 9 tracks at 60 Hz, 20000-frame cap,
     ≈170k frames, `/var/tmp/cmr-soak/b2real-san12`): 0 AddressSanitizer, UBSan, fatal or
     non-finite lines.
   - 12 humans on LAN under ASan/UBSan: `Tests/NetworkSmokeTests.py build-cmr-san/CroMagRally 12`
     (host + 11 clients, 600 frames, plus a 13th join that must be refused) passes on Desert,
     Egypt and Atlantis, in about 16 s each. The in-process readiness and lifecycle tests run
     at `MAX_CLIENTS` = 12 in ctest.
5. **Start-slot CTF balance** (minor, from the last verification). Red and green
   extras on TarPits, Ramps, Celtic and Spiral differ by ≈1.8–2.3k in drivable
   path to their torches. There is a work-in-progress generator change in
   `docs/wip/start-slots-ctf-balance.patch`. It is unverified: the table was not
   regenerated with it.
6. ~~CHANGELOG entry for branch 2~~ done (Unreleased section).

## Open decisions for the owner

- Tournament `placeToWin` (`Main.c:393-396`): 3rd of 12 on Easy, 1st otherwise. Keep it or scale it?
- Battle length at 12: worst case for Tag1 is (N−1) × duration, and CTF keeps 6 flags per team. Scale them?
- Minimap: a centre dot on second wearers, or six extra colours?
- Spanish, German and Swedish use the English ordinal sprites (original behaviour). Switch Spanish to º/ª?
- Sound channels: 40 at 12 (Europe peaks at 43), or the charter's 32?
- Particle groups: 100 at 12 (stock gating ratio), or fully proportional (140)?
- Skid pool: the per-car share is unchanged, and the pool is full by design at both 6 and 12.
- Scoreboard downgrade: a 6-player build drops 7th–12th records written by a
  12-player build. Accept that, or validate against a fixed bound such as 16?
- Humans on the character screen may still pick the same look as each other; only CPUs are re-dressed.

## Pre-existing issues noticed (not changed)

- `HandleCollisions` never advances `oldNumCollisions`, so a trigger can be handled up to 3 times per call.
- `MakeSnow` returns from its per-player loop early.
- `ShowFinalPlace` hides the lap and timer icons for only one frame.
- `gPlayerNameStrings` is never filled, so the LAN "xxx WINS" text shows a blank name.
- Near-simultaneous finishes can freeze two cars on the same `place`.

## Local artifacts (this machine)

- Worktrees: `/var/tmp/cmr-wt/<unit>`, with local branches `wip/*` (never pushed).
- Unit reports: `/var/tmp/cmr-tools/stageA-results.json`.
- 12-car measurement patch for branch 1: `/var/tmp/cmr-tools/meas12-on-b1.patch`.
- Soak logs: `/var/tmp/cmr-soak/`.
- Screenshots: `/var/tmp/cmr-b2-hud-shots/`, `/var/tmp/cmr-b2-drivers-shots/`, `/var/tmp/cmr-slot-previews/`.
