# feat/12-players: work-in-progress status (2026-09-22)

This branch implements charter §4 (12 players) on top of `fix/player-limit-gaps`
(PR #42, merged to master as `c92e559`). Work stopped part-way when the session's
usage quota ran low. **`MAX_PLAYERS` is now 12** (commit "Raise the player limit to
twelve"); every earlier change is written for any `MAX_PLAYERS` and is a no-op, or
bit-identical, at 6 cars. Delete this file before merging.

The protocol cookie stays `CMR8`: it is unreleased (v3.1.x ship `CMR7`), so
branch 2 doesn't bump it (owner decision, 2026-09-22).

**Stacked on #44** (owner decision, 2026-09-23): `feat/cpu-slot-fill` is merged into this
branch so the player-limit setting can share its prefs layout, config message and
Settings > Gameplay menu. Merge #44 first; this PR's diff then shrinks to its own work.

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
  - The second wearer of each outfit gets its own minimap colour (owner decision; this
    replaced a centre-dot marker).
- **Budgets** (§4.6–4.7):
  - sound channels: a fixed 64 (owner decision);
  - skid marks: 40 per player (owner decision);
  - particle groups: 70 per six players, with scaled snow and bubble reserves;
  - collision list: kept at 60 and asserted;
  - a note that the supertile pool scales with panes, not cars;
  - scoreboard places are bounded by a fixed 16, not the player limit (owner decision).
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
- **6 / 12 players setting** (owner request, 2026-09-23): Settings > GAMEPLAY > PLAYERS,
  "6 [ORIGINAL]" (default) or "12" (the game fonts have no parentheses). Single-player,
  split-screen with CPU fill, and LAN hosts use it; a LAN host sends it in its game
  config (`NetConfigMessage.playerLimit`) and caps its lobby at it, and clients use the
  host's value for that match only. Stored in prefs v2 (unreleased, so no v3; a dev build
  of #44 alone resets its prefs once). The demo always uses 6.
- **CPU rescue** (owner decision, 2026-09-23): a car the AI drives that goes 20 s without
  forward progress (no new checkpoint, never 500 units closer to the next) is put back
  just past where it last crossed a checkpoint going forward, facing that way. Soak (9
  tracks × 50/60/72 Hz × 12 and 6 cars): stranded CPUs 1 → 0, time stuck −12%, worst
  car's share 18% → 13%, lap times unchanged; 197 rescues in 54 races (a few cars loop
  2–9 times on one segment before getting through). Checkpoint midpoints and AI path
  points were tried first as rescue spots and trapped cars again.
- **Smoke-only soak flags** (§6; two self-contained, droppable commits):
  - Flags: `--smoke-autopilot`, `--smoke-cars`, `--smoke-fixed-fps`,
    `--smoke-seed`, `--smoke-until-finish`, `--smoke-metrics`.
  - Scripts: `tools/run_race_metrics.sh` and `tools/analyze_race_metrics.py`.
  - Output matches the old env-var harness exactly: 786 of 786 values.

Each unit passed the CI-equivalent run (normal build + ctest, `-DSANITIZE=ON`
build + ctest, `StartupSmokeTests.py`, `MalformedAssetTests.py`) on its own branch.
The integrated branch passes it too: 27/27 ctest normal and sanitizer, 101
StartupSmoke PASS lines, MalformedAssetTests PASS. GitHub CI on PR #43 passes on every platform (Linux GCC/Clang/ARM64 with the sanitizer smoke, Windows, macOS, iOS/tvOS, Android).

## Not done (charter §4)

1. ~~§4.1 constants and wire~~ **done**: `MAX_PLAYERS` 12 (`MAX_CLIENTS` follows),
   `kNSpMaxPayloadLength` 1024, `NET_MAX_PENDING_EVENTS` 12, an exact-size assert
   for the 628 B host control message, an 80 KB send ring (~2 s of host messages at
   60 fps), and updated README and netcode notes. No 4CC change. CI-equivalent run:
   27/27 ctest normal and sanitizer, 101 StartupSmoke PASS lines, MalformedAssetTests PASS.
2. ~~CPU strandings on fences~~ **done**: the CPU rescue above.
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
5. **Start-slot CTF balance: dropped by the owner.** Red and green extras on TarPits, Ramps,
   Celtic and Spiral differ by ≈1.8–2.3k in drivable path to their torches (under a second of
   driving). Revisit after live testing; the unfinished generator change was removed.
6. ~~CHANGELOG entry for branch 2~~ done (Unreleased section).

## Owner decisions (2026-09-22)

- Tournament `placeToWin`: **kept as is** until live testing.
- Battle length at 12 players: **elimination tag scaled** so a 12-player game lasts as long as a
  6-player one; stampede tag, survival and CTF unchanged. Done.
- Minimap: **six more colours** in the current style (violet, magenta, yellow, cyan, dark
  green, navy for the second wearer of brown, green, blue, gray, red and white). Done.
- Spanish, German and Swedish ordinals: **kept** as they are (original behaviour).
- Humans picking the same look on LAN: **accepted**, since it matches current behaviour.
- Sound channels: **as many as necessary**, so a fixed 64 for every build. Done.
- Particle groups: **proportional** (70 per six players, so 140 at 12). Done.
- Skid-mark pool: **raised** to 40 per car (240 at 6, 480 at 12). Done.
- Scoreboard: **fixed bound** of 16 places, so 6- and 12-player builds share records. Done.
- CPU rescue for cars wedged on fences: **implemented** (2026-09-23).
- CTF start-slot balance: **dropped** (under a second of driving; revisit after live testing).
  The unfinished generator patch was removed.
- LAN smoke in CI: **added** (six players, 300 frames, Linux/GCC sanitizer job).
- Submarine collision, latched planing/grease afloat, and the displacement-based stuck check:
  **left as they are** (the stuck check goes with the deferred fence rescue).

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
