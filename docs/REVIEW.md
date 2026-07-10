# Cro-Mag Rally Fork — Holistic Review

**Repo:** `jm2/CroMagRally` (master @ 253f74a) · **Baselines:** `jorio/master`, `jorio/net`, original 1999 Pangea source (997b783)
**Date:** 2026-06-11 · **Method:** 5 mapper agents → 22 adversarial verifiers (refute-biased) → 5 fresh-lens hunters → completeness critic → 3 competing WiFi designs → 3-lens judge panel → synthesis. Every finding below was independently re-derived from code with file:line evidence; severities reflect the *verified* assessment, which in several cases differs from the initial claim.

> **Historical review snapshot:** This document describes commit `253f74a` as it existed on June 11, 2026. It is retained as engineering provenance; many findings and “pending” items were addressed by later CMR7 and hardening commits. Treat the current source, tests, and changelog as authoritative for present behavior.

---

## 1. Executive summary

The fork is in better shape than its provenance suggests: the core lockstep determinism model is sound *by construction* (clients only ever simulate host-echoed inputs), the "Restore legacy physics" commit (0e4c042) genuinely restores 1999 values everywhere it touched, and weapons/items/terrain constants are verified identical to the original. The big problems cluster in four areas:

1. **Wire safety (security):** the host writes to arrays using attacker-controlled indices from network payloads — remotely exploitable memory corruption on any LAN. 2 critical, 4 high findings.
2. **Session-killer netcode bugs:** never-reset state (queues, strike counter) makes the *second* hosted game in a process reliably die; lobby churn wedges races; several disconnect paths call `DoFatalAlert` and kill everyone.
3. **Unintentional gameplay-fidelity regressions:** the RNG split into synced/unsynced streams (a2c3b97) misrouted ~8 *simulation-affecting* random draws to the unsynced per-machine stream — traps, land mine, oil slick, stump geometry, sub AI — creating real cross-peer divergence the rubber-band only partially masks. Plus one new behavior (cactus car-spin) that contradicts the fork's own legacy-restoration intent.
4. **WiFi stutter root cause:** the per-frame blocking lockstep barrier converts any single client's radio jitter into a global freeze, and the 30-frame pre-roll buys jitter absorption at a constant ~500ms input lag. The fix is not tuning — it's removing the barrier. A complete, judge-vetted redesign ("CMR7 rev B") is in §5 and `wifi-design-synthesis.md`, estimated 20–30 dev-days, staged so each stage ships independently.

A handful of changes are **intentional tuning you should explicitly ratify or revert** (§4.3): WATER_FRICTION 1600→1000, ice acceleration 0.3→0.5, the PCG32 RNG swap, and the cactus car-spin.

---

## 2. Critical & high: wire safety

The custom NetSprocket layer authenticates `message->from` (host forces it from the socket, NetLow.c:821-824 — verified good) but **never validates payload-embedded indices or message lengths**. On a LAN this is reachable by any device that can connect to port 49959 or spoof a UDP lobby datagram.

| # | Sev | Finding | Where |
|---|-----|---------|-------|
| W1 | CRITICAL | `cMsg->playerNum` (int16, attacker-controlled) used as unchecked index → wild OOB **writes** on host: `sHostInputQueues[playerNum]` (~150-byte records at arbitrary offsets), `gPlayerInfo[i].controlBits`, RecoverFromFuture | NetHigh.c:1230, 1238, 1107-1117, 1136-1143, 1147-1162 |
| W2 | CRITICAL | Same pattern in `kNetPlayerCharTypeMessage` handler — on host **and all clients**; the code itself says `// TODO: Check player num` | NetHigh.c:374-380 |
| W3 | HIGH | No per-type length validation: a 28-byte message claiming `kNetClientControlInfoMessage` is cast to a ~148-byte struct → heap over-read (same for the ~248-byte host control struct on clients) | NetLow.c:725-731, 762-763; NetHigh.c:1229, 882-908 |
| W4 | HIGH | Host blindly relays any `kNSpAllPlayers` message; unknown/internal types hit `DoFatalAlert` (NORETURN) in `HandleOtherNetMessage` → one packet remotely crashes host **and** all clients | NetLow.c:827-831; NetHigh.c:1417-1499 |
| W5 | HIGH | `NSpGame_AcceptNewClient` scans `i < MAX_PLAYERS` (6) over `players[MAX_CLIENTS]` (4) → OOB read on full lobby, conditional OOB heap write (only loop in NetLow.c with the wrong bound) | NetLow.c:472, 485-490, 83 |
| W6 | HIGH | Client trusts host-supplied `numPlayers`/`playerNum` in the config message (int8, up to 127) → OOB write loop over `gPlayerInfo[6]`; `gMyNetworkPlayerNum` then used unchecked everywhere | NetHigh.c:644-658, 1070 |
| W7 | MEDIUM | Lobby UDP discovery validates **nothing** — the receiver never checks the `JOIN MY CMR GAME` payload it broadcasts (`// TODO: Check payload!!`), and the client auto-joins `gamesFound[0]` | NetLow.c:1193-1231, 1638; NetHigh.c:303 |
| W8 | MEDIUM | `kNSpPlayerLeft` with unknown playerID → `DoFatalAlert` (remote DoS, client-relayable via W4) | NetHigh.c:1513-1523 |
| W9 | MEDIUM | Client trusts `message->to`/`playerInfo.id` in join handlers → assert-crash / OOB read | NetLow.c:867-895 |
| W10 | LOW | `players[id]` indexed where `players[slot]` computed (works only because `kNSpHostID==0`); unchecked `1 << playerID` shifts | NetLow.c:1497-1501; NetHigh.c:107,112 |

**Fix shape:** one `ValidatePlayerNum()`/length-table check at the dispatch boundary covers W1–W3, W6, W8–W9; W4 needs a relay whitelist; W5 is a one-character bound fix. All are independent of the netcode redesign and belong in its Stage 0.

---

## 3. High: session-killer netcode bugs

These are the bugs behind "second game never works" and mystery mid-race fatals. All verified, most fork-introduced in 9caf74e ("Robust WiFi Network Protocol").

| # | Sev | Finding | Where |
|---|-----|---------|-------|
| N1 | HIGH | **Stale host input queues:** `sHostInputQueues` survives `EndNetworkGame`; game 2 resets counters to 0, stale heads look like "future" packets forever → host wedges ~8s → fatal for everyone. Near-deterministic for a WiFi host with 3+ players hosting a second race in one app session; recovery requires app restart | NetHigh.c:1103, 1189-1214, 188-224 |
| N2 | HIGH | **Sync-mask conflation:** host marks sync by `playerNum` (dense rank) but `AreAllPlayersSynced` XORs against an NSp-ID (slot) mask; the host marks *itself* by NSp ID. Any lobby churn (join-then-leave before start) → non-contiguous slots → first race frame wedges 8s → fatal. Upstream used `MarkPlayerSynced(inMess->from)` — this is a fork regression | NetHigh.c:115-120, 1175, 1184-1216; NetLow.c:1425-1451 |
| N3 | HIGH (was reported medium) | **Host strike counter never reset:** `gTimeoutCounter`'s only reset is in *client-only* code (NetHigh.c:857). 4 cumulative 2-second stalls across the host's entire process lifetime — across races and games — and every subsequent 2s stall is instantly fatal. The 1999 original reset it per session; the fork dropped that | NetHigh.c:45, 68, 1252-1259 vs 997b783 network.c:184 |
| N4 | CRITICAL (crash) | **Lobby disconnect crash:** a client disconnecting from the lobby *before* game start synthesizes `NSpPlayerLeft` → `PlayerUnexpectedlyLeavesGame` → `FindHumanByNSpPlayerID` finds nothing (IDs assigned only after lobby) → `DoFatalAlert` → host process exits | NetLow.c:797-815; NetHigh.c:1513-1523 |
| N5 | HIGH | **Disconnect inside pause menu crashes both sides:** the pause callback receives (which may tear down the net game) then *unconditionally* sends; `GAME_ASSERT(gIsNetworkClient)` fires after teardown — the `TODO: If net game died, bail here` comment marks the exact spot | Paused.c:150-164; NetHigh.c:1039 |
| N6 | MEDIUM | Kick-on-send-failure never notifies the host's own app layer: ghost "human" player, and a kicked-while-paused client's stale `pauseState=1` can NET-PAUSE everyone indefinitely | NetLow.c:1788-1817, 1845-1906; NetHigh.c:822-825 |
| N7 | MEDIUM | "Everybody left" flow is dead: `gNumRealPlayers--` commented out so `gGameOver` never triggers on departures; a 2-player game whose peer quits silently continues vs a bot, end screen never appears | NetHigh.c:1529-1533 |
| N8 | MEDIUM | Already-synced player disconnecting in the same receive window leaves a stale bit in `gPlayerSyncMask` → wedge → fatal for everyone instead of bot conversion. Same pattern in vehicle-select wait, which has **no timeout at all** | NetHigh.c:115-120, 1179-1262 |
| N9 | MEDIUM | Abort/error during vehicle-select is swallowed (`GetVehicleSelectionFromNetPlayers` ignores the abort result) → game starts anyway as a broken 2–4-pane pseudo-splitscreen after net teardown | NetHigh.c:1398-1405; SelectVehicle.c:90-92; Main.c:1315-1318 |
| N10 | MEDIUM | Ungraceful client teardown leaks the established FD bound to 49959; no `SO_REUSEADDR` anywhere → next host attempt fails `EADDRINUSE` | NetLow.c:1314-1357, 1915-1922, 575-580 |
| N11 | MEDIUM | Both lockstep waits are zero-sleep busy-spins with no SDL event pump: one core pegged, OS "not responding" dialogs during any stall (bounded ~8s, but ugly and battery-hostile on mobile) | NetHigh.c:923-974, 1179-1262 |
| N12 | LOW | Second pause path in `PlayArea` is provably unreachable (f548023's `schedulePause = false` patch made the net-aware path dead); cosmetic banner artifacts, plus a maintainability trap | Main.c:1134-1138 vs 1149-1157 |

Also verified as designed-but-costly (input to §5): the **~500ms pre-roll** (Main.c:1008-1018) is a permanent, never-draining latency floor for all clients whenever `gUseRedundancy` is on — and the connection hint hardcodes iOS/macOS to "WiFi," so any game involving an Apple device pays it regardless of actual wiring. The **8-frame wire redundancy** is dead weight on ordered TCP (gaps were always counter bugs, never loss), and `RecoverFromFuture` actively *zeroes* `controlBits_New`/`pauseState`, eating button edges. `Client_CheckIfMorePacketsWaiting` (NetHigh.c:982-1023) is dead code since 9caf74e removed its only caller; frame-skip catch-up went with it. `SendOnSocket`'s retry loop stalls the main thread ~198ms per no-progress streak — and the cap is *soft* (resets on partial progress), with worst case ~800ms across a 4-client broadcast.

---

## 4. Gameplay fidelity vs jorio/master, jorio/net, and the 1999 original

This was the explicit cross-check you asked for. Three buckets.

### 4.1 Verified clean (no action)

- **0e4c042 "Restore legacy physics" is genuine:** cactus prop spin, SnoMan, CampFire, car-vs-car binary threshold (relSpeed>1200) all match 997b783 exactly (the ChaoticFloat conversions preserve original ranges — with one exception, below).
- **Weapons/items/terrain constants are identical to 1999:** no damage/speed/timer constant changed in Player_Weapons.c; tile-attribute physics (water/ice/snow friction-traction-steering) match across all three baselines except the two deliberate changes in §4.3; Terrain2.c, Checkpoints.c, Fences.c, Liquids.c, Paths.c have zero diff; Terrain.c's scary 2,276-line diff is clang-format noise plus a mobile-only render path.
- **The "critical math error" RNG fix (a2c3b97) was a no-op** — verified bit-identical over 5M draws. The misleading commit message is the only defect (it can misdirect future bisects).
- **Submarine stuck-evasion system, skeleton code, item spawn parameters:** no formula/constant changes beyond the RNG-stream migrations below.
- Difficulty-gate (`gDifficulty`) and spline-pause changes are upstream jorio/net, not fork divergences.

### 4.2 Unintentional regressions (fix these)

**The unsynced-RNG migrations.** Commit a2c3b97 split RNG into a synced sim stream and a time-seeded per-machine "visual" stream, but misrouted these *simulation-affecting* draws to the unsynced stream. In lockstep, every peer simulates all of these — so peers compute **different physics**:

| Site | Effect of divergence |
|------|----------------------|
| Catapult rock speed/aim — Traps.c:727-733 | Different rock arcs per peer → `MakeBombExplosion`/`BlastCars` impulses (up to ~450 xz / +1600 y, Player_Car.c:3230-3232) fire at different places/times. Rubber-banding corrects position at 0.2/packet but **never velocity/health/timers** |
| Cannonball aim — Traps.c:1191 (the :1194 `Delta.y` draw is a dead store, overwritten at :1197) | Same class |
| Goddess lightning offsets — Traps.c:948, 950, 972-974 | Blast at randomized endpoint (±300/±100 jitter vs radius 300) diverges per peer |
| Land-mine victim spin — Player_Weapons.c:1860-1861 | `DeltaRot.y` integrates into heading (Player_Car.c:1638); upstream used synced `RandomFloat2` at the same lines |
| **Oil-slick scale — Player_Weapons.c:~629-631** (found in the second fidelity pass) | Scale defines the slip hitbox (±30·s rect → `greasedTiresTimer=1.3`) — peers disagree on whether you hit it |
| Stump model/rotation — Items.c:838, 843 | Chosen model drives the **collision box** → permanently divergent solid geometry per peer |
| Sub stuck-evasion side-force — Player_Submarine.c:608 | Direct sim input (`analogSteering.x`); only matters when a dropped player becomes a sub bot, but then it can cascade into a fatal seed-desync via conditional synced-RNG consumption in CPU powerup logic |
| Team-torch landing offset — Triggers.c:~1438-1439 | ±100 placement divergence in team modes |

Important scoping correction from verification: **net games start with zero CPU players** (the 1999 source already commented out the race-mode CPU case), so the AI-related sites only fire after a mid-race disconnect converts a slot to a bot. The traps/mine/oil/stump sites fire in *every* net game on the relevant tracks. None of these can be refuted as intentional: upstream used the synced stream at the same lines.

Divergence consequences are real but bounded: car position/heading is pulled back by the 0.2/packet rubber-band; the fatal risk is indirect (divergent state → different *count* of synced-RNG draws → `kNetSequence_SeedDesync` aborts the session). This is the mechanism behind "random" desync fatals.

**Cactus car-spin asymmetry.** The fork adds car spin on cactus hits (no baseline does this — see §4.3), and the 0e4c042 ChaoticFloat conversion dropped the `-0.5` centering offset used at every other spin site, making both yaw and roll kicks positive-only (`[0..+5)`, `[0..+2.5)` rad/s) — a clear oversight given the author's own conversion comments at Triggers.c:1122-1124.

**ChaoticFloat is not net-safe despite its comment** (Misc.c:240-241): inputs include float state that diverges across peers (and the float→uint32 cast is UB for negative seeds — latent, all current call sites pass non-negative). Verification *refuted* the claimed `gSimulationFrame`-drift mechanism — frame counters provably stay lockstep-identical — but divergence through its float arguments stands.

**MoveSplineObjects pause gate is inverted** relative to `MoveObjects`: during pause it moves nodes *without* `STATUS_BIT_MOVEINPAUSE` and freezes nodes *with* it (SplineItems.c:325-330 vs Objects.c:379-380). The gate itself came from jorio/net; the inversion is the bug.

**Lost in merge a5f48a9:** five jorio/net CLI flags (`--host`, `--join`, `--port`, `--display`, `--windowed-resolution`) — parsed nowhere, consumers dead, port 49959 now unconfigurable. ⚠️ Note: the synthesized test plan in §5 assumes `--host`/`--join` work, so restoring them is a prerequisite for the netem harness (the synthesis lists this; one agent believed they worked — the verifier's dead-code proof is the authoritative one).

### 4.3 Intentional tuning — ratify or revert

These are documented, deliberate, and behavior-changing vs both upstream and 1999. Your call as designer; my recommendation in italics:

1. **WATER_FRICTION 1600→1000** (c1576a5). Fixes a genuine upstream softlock — the Mammoth (lowest accel) had net *negative* thrust on water and could stall permanently at full throttle. Top speed unchanged (WATER_MAXSPEED=2000 still caps); coast-down 1.25s→2.0s. *Keep, but consider the minimal fix (raise only low-stat effective thrust or lower friction to ~1400) if water feel vs 1999 matters to you.*
2. **Ice groundAcceleration 0.3→0.5** (same commit). Affects **all** cars (~67% more thrust on ice, low-speed boost amplified 1500→2500), not just low-stat ones; changes race balance on ice tracks. *This one overshoots its stated purpose — consider 0.4, or a low-stat-only floor.*
3. **PCG32 RNG swap** (59c6069). Breaks raw-sequence compatibility with 1999 (verified: a2c3b97 was still bit-identical; 59c6069 is the breaking commit). No consumer of the original sequence exists (wall-clock seeding, no replays); cross-platform determinism is genuinely improved; `RandomFloat` range changed from [0,1] inclusive to [0,1). *Keep.*
4. **Cactus car-spin** (d30cbe6, halved in 8d8ab0a). New gameplay vs every baseline — original spins only the cactus prop. *Decide: if you want it, fix the positive-only bias (§4.2); if "true to original" wins, delete the whoNode spin.*
5. **Tag duration no longer persisted** (prefs field retired → plain global, resets to 3 each launch; net-host value also leaks into later local games). *Probably unintended side effect — restore persistence.*

---

## 5. WiFi multiplayer: root cause and the CMR7 redesign

### Root cause (verified)

Current model: single-threaded TCP lockstep where the **client busy-blocks at top of frame** for the host packet and the **host busy-blocks at end of frame** for *all* clients' inputs. Consequences on WiFi (50–200ms jitter spikes from radio retries/power-save/contention):

- Any one client's uplink spike → host stalls → **everyone** freezes (and earns a never-reset strike toward the session-fatal, N3).
- A client's downlink spike → it freezes, then plays back time-dilated *forever* (1 packet/frame, no catch-up since 9caf74e removed frame-skip) — and its uplink goes silent during the stall, so the spike propagates to everyone anyway.
- The pre-roll "fix" buys 500ms of jitter absorption at a constant 500ms input lag, auto-enabled for any game involving an Apple device.
- `SendOnSocket` can add ~200-800ms main-thread stalls under send pressure.

So the stutter is architectural, not parametric. No tuning of the current barrier makes WiFi smooth.

### The design (unanimous judge verdict; full spec in `wifi-design-synthesis.md`)

**CMR7 rev B — "Free-Running Lockstep":** keep TCP, keep the star topology, keep variable timestep and the fatal seed check — but **the host never waits**. Three mechanisms:

1. **Host-side input substitution + coalescing:** if a client's input isn't there, the host holds their last input for that frame (flagged on the wire), and since clients only ever simulate the host echo, this **cannot desync** — it changes *which* inputs enter the shared sim, never *whether* machines agree. Burst arrivals are coalesced with per-step edge re-derivation (`(bits ^ old) & bits`) so no button press is ever lost. `controlBitsNew` leaves the client wire entirely.
2. **Per-client adaptive input delay D_i (1–8 frames)** sized from measured P95 arrival jitter — wired clients converge to D=1 (≈34–38ms own-input delay, vs ~25–35 today), WiFi clients to D=2–4 (≈55–90ms, vs ~520ms today). Replaces the all-or-nothing `gUseRedundancy` mode and the 500ms pre-roll. Mixed LAN is the headline win: **each client pays only its own link's latency**; a WiFi peer's radio behavior is fully decoupled from the wired players' experience.
3. **Wall-clock-paced client input sender** (decoupled from packet consumption) + free-running client receive with bounded catch-up (K_max=3 packets/render-frame, each replayed with its own dt → bit-identical trajectory) + hold-last-frame for downlink gaps. The continuous 60pps stream doubles as the WiFi radio keepalive.

Plus: non-blocking sends (32KB rings replacing the retry-stall loop), frame-aligned bot conversion on player leave (fixes a proven desync where tag-mode `ChooseTaggedPlayer`'s synced RNG draw could land on different frames per machine), `lastHeard`-based timeout policy replacing the 4-strike counter, and one protocol bump CMR6→CMR7 with `_Static_assert`ed wire structs and host-side bounds validation (closing §2).

**200ms WiFi spike, before vs after:** today, everyone freezes 200ms (uplink spike) or the client freezes + lags forever (downlink). After: nobody stalls; the affected car coasts ~12 frames on others' screens and rubber-bands back (uplink), or only the affected client holds ~200ms then catches up at 3× for ~100ms (downlink) — others see nothing.

**Honest residual:** a true TCP RTO still causes one ~200–300ms *localized* hitch (expected <1/10min on 5GHz, a few/hour on congested 2.4GHz). The UDP transport (Design 2) is retained as a named, telemetry-triggered contingency behind the same pump/queue API — swap confined to NetLow.c if soak data shows >2 multi-frame substitution events/min on healthy 5GHz. State streaming (Design 3) was evaluated and rejected for this step (10–14 weeks, gameplay-wide regression surface, and a *wired* remote-view regression vs lockstep's exact positions); it's documented as the endgame only if internet play is ever wanted.

**Stages** (each independently shippable; 20–30 dev-days total):
- **Stage 0 (3–4d):** the §2 bounds checks, §3 state resets (`ResetNetGameTransientState()` called from both init and end), sync-mask fix, §4.2 RNG re-syncs, sleep+event-pump in the existing spin loops, restore `--host`/`--join`/`--port`, build the 3-netns netem test harness + telemetry. *Shippable as a patch release that already fixes "second game dies," lobby-churn wedges, and the desync-fatal class.*
- **Stage 1 (2–3d):** non-blocking send rings — biggest stall-removal per line of code.
- **Stage 2 (5–7d):** CMR7 bump, host-never-waits, adaptive depth, wall-clock sender, edge re-derivation. *The stutter win.*
- **Stage 3 (4–5d):** client free-running + catch-up + hold-last. Profile K_max on the weakest Android target early (terrain supertile streaming is the real cost).
- **Stage 4 (3–4d):** timeout/pause/leave policy, gTargetFPS negotiation fix (current LCD negotiation is asymmetric — machines can end with different caps), Android `WIFI_MODE_FULL_LOW_LATENCY` lock.
- **Stage 5 (3–5d):** cross-arch x86↔ARM soak (catch-up bursts exercise FP paths hardest; pin `-ffp-contract=off`).
- **Stage 6 (3–4d, optional):** true visual extrapolation (the synthesis specifies the `BaseTransformMatrix`/wheel-chain mechanism the first design draft got wrong). Ship only if hold-last feels insufficient.

Acceptance profile: mixed LAN with one congested-2.4GHz peer — wired client frame-time p99 ≤ 1.2 frames and own-input delay ≤ 40ms while the WiFi peer suffers; no global stall > 50ms attributable to the WiFi peer.

---

## 6. Platform & mobile findings

**Android**
- **Asset extraction can permanently brick an install (medium):** the "already extracted" sentinel is written at ~38% of the 153MB copy (gamecontrollerdb.txt is entry 103/274 in unsorted `find` output); any interruption past that point marks extraction complete forever; per-file write failures are silently ignored; nothing re-extracts on app update (versionCode frozen at 1 and never consulted). Fix: write a version/manifest sentinel *after* the loop, check `SDL_SaveFile` returns, preflight disk space. (Boot.cpp:158-204, build_android.sh:161)
- **GL context loss unhandled (high):** zero handling of `SDL_EVENT_RENDER_DEVICE_RESET`/`TERMINATING`/`LOW_MEMORY`; one GL context for the program lifetime; every frame asserts on `SDL_GL_MakeCurrent`. Backgrounding on devices that destroy contexts = crash or black screen. (OGL_Support.c:343-373, 536-538)
- **gl4es initialized against a throwaway boot context** that the game never renders with, and never destroyed; no context sharing set. Works by accident of gl4es's NOEGL mode. (Boot.cpp:286-305)
- Leftover CAMERA permission in the manifest; per-frame heap allocations in touch-finger validation (`SDL_GetTouchFingers` per device per finger, every frame, even in menus).

**Net + mobile lifecycle (high):** backgrounding a phone during a net game kills the *entire session* for everyone in ~8s (DATA_TIMEOUT×4) — no lifecycle-triggered pause, no resume. `SDL_EVENT_TERMINATING` unhandled means mobile kills skip `CleanQuit` → prefs/player-file not saved. The CMR7 design documents a 30s pause-grace as future work; the Stage 4 `lastHeard` policy is the hook.

**iOS**
- **Local-network permission flow is broken both directions (high):** joining never triggers the permission prompt (discovery only `recvfrom`s — iOS silently drops inbound broadcasts for unpermissioned apps → client finds 0 games forever), and hosting errors out of the lobby on the *first* failed `sendto` while the prompt is still on screen. Fix: send a dummy datagram on the join path to trigger the prompt; treat `EHOSTUNREACH` as retry-not-fatal during lobby advertise. (NetLow.c:1108-1235, 1658-1672; NetHigh.c:237-243) — *this also blocks Stage 5 WiFi soak testing on iOS, so schedule it early.*
- Game Center entitlement declared with zero GameKit code, and the entitlements file isn't wired into the build at all.

**tvOS**
- **Prefs, scoreboard, and tournament progression land in purgeable Caches** (SDL_GetPrefPath → NSCachesDirectory on tvOS): the OS can silently wipe high scores any time. Needs NSUserDefaults/iCloud KVS (500KB persistent budget) for saves. (Misc.c:401-417, File.c:552-669)
- Siri-Remote slot logic is name-string fragile (`strstr(name,"Remote")`) and `CompactGamepadSlots` partially undoes the demotion — works in single-player by accident.

**Touch/input (cross-platform)**
- Any keyboard/gamepad event destroys the virtual touch joystick — device detach, all fingers wiped, recreated only on next touch. Initial claim of "breaks mixed touch+gamepad multiplayer" was *corrected* by verification: touch could never drive a non-P1 player anyway (that exclusivity predates this commit); the real harms are attach/detach flapping, OS key-repeat triggering it, finger-state loss, and a latent stale-`gVirtualInput` injection. (Input.c:770-798, 148-159)
- Virtual-gamepad slot eviction only handles slot 0 → a second physical pad can be dead in split-screen on touch devices. (Input.c:1123-1203)
- `UserWantsOut` rewrite drops Start-button dismissal of win/credits/help screens. (Input.c:1031-1048)

---

## 7. Minor findings (collected)

- Tournament objective label in main menu reads `gDifficulty` before it's synced from prefs (stale until first game). (MainMenu.c:480-489)
- `LocalizeWithPlaceholder`/`AdvanceTextCursor`: snprintf-truncation pointer advance → wild-pointer write on truncation (latent, needs long localized strings). (Localization.c:90-135, Misc.c:516-523)
- SCROLLLOCK infobar toggle silently dead in net games (call site gated, contradicting the function's own internal guard ordering). (Main.c:916-947, 1142-1145)
- Goddess lightning *damage point* also on local RNG (visual-flavor cousin of the §4.2 strike-offset issue).
- Throw-grunt SFX selection moved to visual RNG — distribution-equivalent and actually *good* hygiene (removes a sim-RNG draw), no action.
- ~18 new network UI strings lack DE/ES/IT translations (English fallback works).
- Dead `Client_CheckIfMorePacketsWaiting`, dead second pause path, 5 orphaned CLI struct fields — cleanup candidates already covered above.

---

## 8. What this review did NOT cover (from the completeness critic)

1. **The forked submodules were never audited** — `.gitmodules` points Pomme at `jm2/Pomme` and adds `jm2/gl4es`; both directories are empty in this checkout (unfetched). Pomme is the privileged platform/asset-parsing layer and gl4es is the entire mobile GL translator. **Recommend: fetch and diff both forks against their upstreams.** (high)
2. The whole-codebase clang-format pass was token-diffed only for the five highest-risk files; the remaining reformatted files got a generic sweep. Note: statements swallowed into comments at Objects.c:147-148/192/196 (dead block, harmless, but proves the hazard class).
3. Input.c's non-touch routing rewrite (`GetLocalKeyStateForPlayer` gamepadSlot decoupling, f51bfd9/04b69ed/178348d) — only touch paths were deeply reviewed; controller-to-slot mapping is a common stuck-input source.
4. MiscScreens.c / PhysicsEditor.c were only generically swept.

---

## 9. Prioritized action plan

| Priority | Action | Covers |
|----------|--------|--------|
| **P0 — patch release** | Stage 0 bundle: wire bounds checks + relay whitelist; `ResetNetGameTransientState()` (queues, strike counter, sync mask, statics); sync-mask playerNum fix; lobby-disconnect and pause-menu-disconnect crash guards; re-sync the 8 misrouted RNG draws; sleep+pump in spin loops; restore `--host`/`--join`/`--port` | §2 W1-W6, §3 N1-N5, N11, §4.2 RNG |
| **P0 — mobile data loss** | tvOS persistent storage; Android sentinel-after-loop + write checks; `SDL_EVENT_TERMINATING` → save | §6 |
| **P1 — the WiFi fix** | Stages 1–5 of CMR7 rev B (send rings → host-never-waits → client free-run → policy → soak) | §5 |
| **P1 — iOS netplay viability** | Local-network permission flow (blocks both casual use and Stage 5 testing) | §6 |
| **P2 — fidelity decisions** | Ratify/revert §4.3 items; fix cactus positive-only bias either way; fix spline-pause inversion; restore tag-duration persistence | §4 |
| **P2 — robustness** | N6-N10 (ghost players, everybody-left, vehicle-select abort, FD leak/SO_REUSEADDR); GL context-loss handling; lifecycle pause-grace | §3, §6 |
| **P3 — audit debt** | Diff jm2/Pomme and jm2/gl4es vs upstreams; Input.c slot-routing review; Stage 6 extrapolation if wanted | §8 |

---

### Companion documents

- `WIFI-NETCODE-CMR7.md` — the full CMR7 rev B WiFi redesign: protocol, message formats, latency math, mixed-LAN handling, fidelity analysis, and the staged implementation plan.
- `SUBMODULE-AUDIT.md` — audit of the forked `extern/Pomme` and `extern/gl4es` submodules vs upstream.

Most of the P0/P1 findings in this review are **already fixed** on this branch (see the
commit history); the items explicitly deferred to "CMR7 Stage 0" are tracked in the WiFi
doc. The raw multi-agent analysis (per-finding verdicts, hunt reports, judge panel, raw
JSON) is kept out of the repo under a local `cromag-review/` working directory.
