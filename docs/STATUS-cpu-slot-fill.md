# feat/cpu-slot-fill: work-in-progress status (2026-09-22)

This branch implements charter §5 (fill empty multiplayer race slots with CPU cars)
on top of `fix/player-limit-gaps` (PR #42, merged to master as `c92e559`), written
against `MAX_PLAYERS` generically. Work stopped part-way when the session's usage
quota ran low. **Split-screen fill is complete and tested; LAN fill is not wired
yet.** Delete this file (and `docs/wip/`) before merging.

The protocol cookie stays `CMR8` (unreleased; owner decision, 2026-09-22). The
network unit's `CMR9 -> CMRB` commit was deliberately left out.

## Done (in commit order)

- **Prefs v2 migration:** a new "CPU CARS" option. Existing v1 prefs load with
  every setting preserved and the option off. Tests cover the v1 upgrade, the v2
  round trip, corrupt and short files, and the tvOS storage path.
- **Race-end rule** (§5.3), in `DecideMultiplayerRaceFinish`, unit-tested:
  - With fill on, CPU finishes never end the race; the first human to finish does.
  - Win/lose is decided among humans only.
  - Humans keep their live overall place.
  - With fill off, behaviour is unchanged.
- **Smoke-only split-screen race:** `--smoke-local-players N`, plus
  `--smoke-cpu-fill`, covered in `StartupSmokeTests.py`.
- **Split-screen fill** (§5.2): `gCPUFillThisRace`; `gNumTotalPlayers =
  MAX_PLAYERS` for `GAME_MODE_MULTIPLAYERRACE`; humans take slots `0..n-1`; CPU
  cars come from branch 1's picker.
- **CPU drivers look distinct from the humans.**
- **UI** (§5.1): a small step after picking RACE in the split-screen game-type
  menu, with a help line, a "CPU CARS: OFF/ON" cycler bound to the saved pref, and
  OK. It never appears for battle modes.
- **Network preparation** (§5.4, partial):
  - `InitSharedCPUVehiclePickRules`: fill CPU cars in a network race are a pure
    function of the humans' cars, difficulty, track and slot, over the whole land
    roster. It ignores local unlocks, and Hard uses `DeterministicStableFloat`,
    with no synced RNG.
  - `DressNetworkFillCPUs`: fill CPUs get the same looks on every peer.
  - Both are tested, but no network race seats fill cars yet.

The integrated branch passes the CI-equivalent run (normal build + ctest 22/22,
`-DSANITIZE=ON` build + ctest 22/22, 76 StartupSmoke PASS lines,
MalformedAssetTests PASS). GitHub CI has not run on it yet.

## Not done (charter §5.4–5.6)

1. `NetConfigMessage.reserved` → `cpuFill`: the host fills it from its pref in
   race mode only; validate 0/1 (and 0 for battle modes) in `NetValidation.c`.
   Clients set `gCPUFillThisRace` and `gNumTotalPlayers` from it. Today
   `PlayGame` keeps fill off in network games (`Main.c`, `gCPUFillThisRace =
   !gNetGameInProgress && ...`).
2. Host-authoritative CPU POW use:
   - `kEvCpuThrow` through `Host_ScheduleFrameEvent` → `events[]` →
     `ApplyPendingFrameEvents`, with the POW type and direction in
     `NetFrameEvent.pad`;
   - clients suppress local CPU throw decisions;
   - update the validator's dedupe rule;
   - size `NET_MAX_PENDING_EVENTS` for one event per non-host player.

   A half-finished version of the encoder, decoder and validator is in
   `docs/wip/cpu-fill-kEvCpuThrow.patch`. It is unverified and doesn't build yet:
   it changes `NetValidateHostControlPayload`'s signature without updating its callers.
3. Seed-desync audit (`NetHigh.c` `MyRandomLong() != mess->randomSeed`): CPU
   fill must add no synced-RNG draws that depend on local state (items and traps
   CPUs trigger, pickups, effects).
4. Tests:
   - `cpuFill` config validation;
   - readiness and lifecycle tests with fill on;
   - an in-process multi-peer race with CPUs, with no seed or position desync;
   - `Tests/NetworkSmokeTests.py --cpu-fill` (host + 1 client + CPUs).
5. §5.6 merge check: test-merge onto `feat/12-players` (4 split-screen humans + 8
   CPUs; host + 1 client + 10 CPUs). The two branches overlap in `Player.c`
   (driver looks: keep one of `ResolveCPUDriverLooks` / the fill dressing),
   `Boot.cpp` and `file.h` (smoke flags), and `BUILD.md`.
6. CHANGELOG entry for branch 3.

## Open decisions for the owner

- Translations of `STR_CPU_CARS` / `STR_CPU_CARS_HELP` (FR/DE/ES/IT/SV) were
  written without native review. Keep them, get them reviewed, or leave them blank
  (blank cells fall back to English)?
- Prefs downgrade: an older build reading v2 prefs resets them to defaults, as
  with any prefs format change. Alternatively the new option could live in a
  separate file.
- UI placement: the step after RACE, or Settings?
- Scoreboard: filled races are recorded like any multiplayer race, with the
  human's overall place among all cars. Records don't say whether CPUs were present.
- Win screen: YOU WIN / YOU LOSE plus the live HUD place. No final-place banner,
  because it overlaps the win text. Is that enough for "each human still sees
  their overall place"?
- With fill on, a network player who left and became a bot can no longer win a
  filled race; without fill a bot can still win, as before. Intended?

## Known gaps

- No automated test drives the new menu step, because the menu system has no
  input harness. It was checked with screenshots from a measurement build.
- CI smoke runs stop at 600 frames, so no race ends in CI. The race-end rule is
  covered by unit and wiring tests plus uncommitted measurement runs.
- Long measurement races showed CPU stalls (Scandinavia 79 s; Ice ≈200 s for an
  autopilot car). This is the pre-existing land-car stranding tracked on
  `feat/12-players`, not fill-specific.

## Local artifacts (this machine)

- Worktrees: `/var/tmp/cmr-wt/b3-local`, `/var/tmp/cmr-wt/b3-net` (local branches `wip/b3-local`, `wip/b3-net`).
- The last `wip/b3-net` commit holds the uncommitted work shown in `docs/wip/`.
- Unit report: `/var/tmp/cmr-tools/stageA-results.json`.
