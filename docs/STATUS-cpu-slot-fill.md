# feat/cpu-slot-fill: status (2026-09-22)

This branch implements charter §5 (fill empty multiplayer race slots with CPU cars)
on top of `fix/player-limit-gaps` (PR #42, merged to master as `c92e559`), written
against `MAX_PLAYERS` generically. **Split-screen and LAN fill are both wired and
tested**, the §5.6 merge check onto `feat/12-players` passes, and the CHANGELOG
entry is written. What remains is the owner decisions below. Delete this file before merging.

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
- **UI** (§5.1): a small step after picking RACE in the multiplayer game-type
  menu (split-screen, or hosting a LAN game), with a help line, a "CPU CARS:
  OFF/ON" cycler bound to the saved pref, and OK. It never appears for battle modes
  or for LAN clients, who follow the host.
- **Shared network CPU cars and looks:** `InitSharedCPUVehiclePickRules` (a pure
  function of the humans' cars, difficulty, track and slot over the whole land
  roster; Hard uses `DeterministicStableFloat`; no synced RNG) and
  `DressNetworkFillCPUs`.
- **The host decides** (§5.4): `NetConfigMessage.cpuFill` (the byte CMR7 retired).
  The host sends its pref in race mode only; the validator accepts 0/1, and 1 only
  for a race. Clients take `gNetGameCPUFill` from the config, never their own
  pref (`DecideCPUFillThisRace`). Reviewed with fill slots: the host's input
  consumer and grace poll skip `isComputer` slots, clients apply the host's bits
  for all slots, readiness barriers work in NSp-ID space (fill CPUs have none),
  leaves convert only network players, and `FindHumanByNSpPlayerID` skips CPUs.
- **Host-authoritative CPU POW use** (§5.4): in a network game only the host runs
  a CPU car's throw decision, and it schedules the use as `kEvCpuThrow` (POW type
  and direction in `NetFrameEvent.pad`; still 8 bytes). Every machine, the host
  included, has the car use that POW at the event's frame if it still holds it.
  Clients never decide. This also covers **replacement bots** (departed humans), in
  races with or without fill, for consistency: their decisions read the same
  positions that can differ between peers. A decision made on a later multipass
  pass is lost, as it is in a local game. `NET_MAX_PENDING_EVENTS` is
  `max(8, MAX_PLAYERS - 1)` (8 at six players, 11 at twelve), because each non-host
  player has at most one event pending. The host reuses a ring slot once its event
  has applied, and never applies an event it couldn't queue. The validator accepts
  a POW use for any car except the host's, and still rejects two events of one type
  for one player. POW pickups can still differ between peers, exactly as they
  already can for humans.
- **Seed-desync audit** (§5.4): the simulation draws the synced RNG only in
  `SetPhysicsForVehicleType` (once per car in slot order at level start; fill seats
  `MAX_PLAYERS` cars on every peer), `ChooseTaggedPlayer` (tag modes, frame-aligned)
  and Hard CPU picks in local games. Weapons, traps, items, pickups, effects, sounds
  and the camera use `VisualRandom*` or `DeterministicSimEventFloat`. **CPU fill
  adds no draw that depends on local state.** The audit found one unrelated bug and
  fixed it: menu cycler and slider sounds drew the synced RNG, so changing a setting
  in a network game's pause menu desynced the seed. `Tests/SyncedRandomTests.py` now
  fails if any unreviewed function draws the synced RNG.
- **LAN smoke with fill:** a host flag, `--smoke-cpu-fill` (with
  `--smoke-net-players`; the host's config carries it and prefs are untouched), and
  `Tests/NetworkSmokeTests.py --cpu-fill`. `--smoke-test-frames` now allows up to
  36000 frames. Each peer's smoke line reports its car count and how many CPU POW
  uses it applied. The script fails unless every peer agrees, and on any sanitizer
  report, `NetGameFatalError`, or seed or position desync.

## Test results

- CI-equivalent run (`ci.sh all`) before every push: normal build + ctest 23/23,
  `-DSANITIZE=ON` build + ctest 23/23, 76 StartupSmoke PASS lines, MalformedAssetTests
  PASS. GitHub CI has not run on the branch yet.
- New unit coverage:
  - `cpuFill` config validation and `DecideCPUFillThisRace`.
  - A filled network race in the readiness harness: the config round trip for both
    host prefs and a battle mode; identical cars and looks on the host and on each
    client (set to the opposite local pref); a mid-race leave beside the fill CPUs.
  - `kEvCpuThrow` encoding, decoding and validation: a full packet at
    `NET_MAX_PENDING_EVENTS`; rejects the host's car, cars outside the race, and
    duplicate uses.
  - Scheduling and apply order:
    - the host's table and a client's table, rebuilt from repeated packets, apply
      identically at the same frame;
    - one use per car is in flight;
    - a slot is reused in the frame its event applied;
    - a full ring schedules and applies nothing.
- LAN soak, sanitizer build, `NetworkSmokeTests.py --cpu-fill --frames 3000`:
  - host + 1 client + 4 CPUs on all nine tracks: all PASS, 6 cars on every peer, CPU
    POW uses 7/8/9/12/6/6/12/8/0 (tracks 1-9), the same count on every peer;
  - host + 2 clients + 3 CPUs on tracks 1, 5, 8 and 9: all PASS, 9/7/7/0 uses;
  - Atlantis (track 9) CPU subs used no POW in 3000 frames, so it also ran 12000
    frames (host + 1 client): PASS with 12 uses;
  - no seed or position desync, `NetGameFatalError`, ASan or UBSan report in any run.
    Unfilled 3-player and full 6-player (join refused) runs still pass with 0 uses.

## Not done

1. ~~§5.6 merge check~~ **done** (2026-09-22). I test-merged `origin/feat/cpu-slot-fill`
   (`4fc9143`) onto `origin/feat/12-players` (`537481d`) in the local branch
   `wip/mergecheck-b3-onto-b2`. It was not pushed.
   - **Conflicts:** 9 files, all resolvable. Mostly the branches add next to each other
     in `CMakeLists.txt`, `file.h`, `Boot.cpp`, `NetValidation.c`, `Main.c`,
     `StartupSmokeTests.py` and `ValidationTests.c`. Decisions made in the merge:
     - `NET_MAX_PENDING_EVENTS` = `max(MAX_PLAYERS, 8)`: 12 at twelve players, and at
       least one per non-host player.
     - The `--smoke-test-frames` limit without soak flags is 36000 (this branch's LAN
       soaks); soak runs keep 100000.
     - In `Player.c`, the CPU car count comes from `CountPlayersInGame` plus the
       `--smoke-cars` override. This branch's network car picker is kept. CPU looks
       come from `DressNetworkFillCPUs` (network fill races) and then feat/12-players'
       `ResolveCPUDriverLooks`.
   - **One real failure on the first attempt:** with only `ResolveCPUDriverLooks`,
     `NetworkFillLooks` failed, because peers whose character screens swapped outfits
     into CPU slots dressed the CPUs differently. Re-dealing the fill CPUs first fixed it.
   - **CI-equivalent run on the merge:** ctest 30/30 on both builds, 114 StartupSmoke
     PASS lines, MalformedAssetTests PASS.
   - **§4 acceptance with fill on, sanitizer build, all 9 tracks, 3000 frames:**
     - 4 split-screen humans + 8 CPUs: clean.
     - LAN host + 1 client + 10 CPUs: clean. Every peer agreed on 12 cars and on
       2–25 host-decided CPU POW uses per track, with no desync.
   - Notes and the resolution diff are in `/var/tmp/cmr-tools/mergecheck-notes.md` and
     `mergecheck-resolution.diff`. Whoever merges second must redo these resolutions.
2. ~~CHANGELOG entry for branch 3~~ done (Unreleased section).
3. No test drives `DoCPUPowerupLogic`'s network path in-process, because
   `Player_Car.c` isn't in the readiness harness. The LAN soak's per-peer POW-use
   counts cover it.

## Open decisions for the owner

- Translations of `STR_CPU_CARS` / `STR_CPU_CARS_HELP` (FR/DE/ES/IT/SV) were
  written without native review. Keep them, get them reviewed, or leave them blank
  (blank cells fall back to English)?
- Prefs downgrade: an older build reading v2 prefs resets them to defaults, as
  with any prefs format change. Alternatively the new option could live in a
  separate file.
- UI placement: the step after RACE (split-screen and LAN host), or Settings?
- Scoreboard: filled races are recorded like any multiplayer race, with the
  human's overall place among all cars. Records don't say whether CPUs were present.
- Win screen: YOU WIN / YOU LOSE plus the live HUD place. No final-place banner,
  because it overlaps the win text. Is that enough for "each human still sees
  their overall place"?
- With fill on, a network player who left and became a bot can no longer win a
  filled race; without fill a bot can still win, as before. Intended?
- When every client leaves a filled LAN race, the host's game still ends
  ("everybody left"), as it does without fill. Should the host keep racing the CPUs?

## Known gaps

- No automated test drives the new menu step, because the menu system has no
  input harness. The split-screen step was checked with screenshots from a
  measurement build; the LAN host step (the same menu page) was not.
- CI smoke runs stop at 600 frames and CI doesn't run `NetworkSmokeTests.py`, so no
  race ends in CI. The race-end rule is covered by unit and wiring tests.
- Long measurement races showed CPU stalls (Scandinavia 79 s; Ice ≈200 s for an
  autopilot car). This is the pre-existing land-car stranding tracked on
  `feat/12-players`, not fill-specific.

## Local artifacts (this machine)

- Worktrees `/var/tmp/cmr-wt/b3-local` and `/var/tmp/cmr-wt/b3-net` (local branches
  `wip/b3-local`, `wip/b3-net`) are superseded by the pushed branch.
- Soak logs: `/var/tmp/cmr-wt/b3/build-cmr-logs/soak/`.
