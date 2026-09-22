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
2. **Land-car strandings at 12 cars** (blocks §4.9 "no stranded cars"). The audit
   found three cases, all with branch 1's afloat fix already applied:
   - on Scandinavia at 12 cars, the autopilot car was stuck 266 s of 494 s at
     lap 0, checkpoint 44, at ≈(90100, 3840, 30900), about 3.8k units above the
     route;
   - a CPU sat ≈90 s near checkpoint 14;
   - a ≈90 s strand also occurs at 6 cars at 72 Hz.

   The stuck check measures raw displacement, so fence-sliding stalls (10–24 s)
   also go unflagged. The investigation unit was stopped before its first commit.
   Logs are in `/var/tmp/cmr-soak/a6-scan12*` and `/var/tmp/cmr-soak/a6-san12`.
3. **§4.8 LAN at 12 humans:** not measured (13 processes with
   `Tests/NetworkSmokeTests.py` after the raise; host downlink, packets/s, input
   grace wait).
4. **§4.9 acceptance:**
   - 12-car ASan/UBSan soaks on the real branch. So far there are ≈287k clean
     frames, but on a measurement build with the prototype's procedural slots.
   - Every CPU completing laps.
   - 7–12 human LAN in-process tests after the raise.
5. **Start-slot CTF balance** (minor, from the last verification). Red and green
   extras on TarPits, Ramps, Celtic and Spiral differ by ≈1.8–2.3k in drivable
   path to their torches. There is a work-in-progress generator change in
   `docs/wip/start-slots-ctf-balance.patch`. It is unverified: the table was not
   regenerated with it.
6. CHANGELOG entry for branch 2.

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
