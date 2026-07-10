# CMR7 — WiFi netcode redesign ("Free-Running Lockstep")

This is the design and implementation plan for making WiFi LAN multiplayer as smooth as
wired without an unacceptable latency cost, while keeping mixed wired/WiFi play smooth.
It was selected unanimously by a 3-judge panel over a UDP rewrite and a state-streaming
design. The P0/P1 fixes that precede it are already on this branch; the **Stage 0** items
below that are still pending are the ones this review deliberately deferred (they need
play-test verification under the redesign).

> **Historical design record:** This plan and its line-number references were written
> before the CMR7 stages landed. “Pending,” effort, and baseline statements document the
> implementation process, not the current state of the shipping code. See the current
> source and `CHANGELOG.md` for implemented behavior.

---
## Design name
CMR7 Free-Running Lockstep, rev B — stall-free host-echo lockstep with adaptive per-client input delay, wall-clock input sender, and frame-aligned game events

## Overview
FINAL DESIGN: Design 1 (CMR7 Free-Running Lockstep) wins as the base — it is the only proposal whose every load-bearing claim verified against this codebase, whose diff stays inside Source/Network/* + the PlayArea loop, and whose determinism argument is correct BY CONSTRUCTION (clients never simulate locally-sampled input: the local write at Main.c:1075-1083 is overwritten by the host echo at NetHigh.c:882-887 before any sim step, so host-side input substitution/coalescing can never desync). Grafted from Design 2 (all three judges demanded these): (G1) WALL-CLOCK-PACED CLIENT INPUT SENDER replacing Design 1's one-send-per-consumed-packet discipline — fixes the verified flaw where a downlink-only spike silenced the client's uplink, forced host substitution visible to ALL players, and killed the in-game radio keepalive exactly during WiFi power-save events; (G2) HOST-SIDE EDGE RE-DERIVATION from the contiguous controlBits stream using the engine's own formula (bits ^ old) & bits (verified InputControlBits.c:145) — controlBitsNew leaves the client wire entirely, eliminating both the double-tap-merge ambiguity of OR-coalescing and the current edge-loss bug (RecoverFromFuture zeroes controlBits_New/pauseState, NetHigh.c:1157-1160); (G3) FRAME-ALIGNED LEAVE/BOT CONVERSION ("player X becomes bot at host frame N" broadcast in the control stream) — fixes the desync judge 2 proved: PlayerUnexpectedlyLeavesGame flips isComputer at TCP-arrival time (NetHigh.c:1525) and in tag modes immediately draws synced RandomRange in ChooseTaggedPlayer (NetHigh.c:1542 → Player.c:621), which under free-running would land D_i+transit frames apart across machines → fatal kNetSequence_SeedDesync. Judge-found errors fixed: send rings sized 32KB (host wire msg is ~290B, not 150B — 16KB held only ~1s, making the stated 2s-kick policy unreachable); substitution does NOT advance gClientSendCounter[i] (otherwise the stale-discard at NetHigh.c:1189-1193 destroys the backlog the coalescer needs) and ackInputSeq means "last REAL input applied"; visual extrapolation is descoped to hold-last-frame in v1 because save/restore of Coord/Rot.y alone renders nothing — drawing consumes ObjNode->BaseTransformMatrix (Objects.c:765) rebuilt only by UpdateObjectTransforms (Objects.c:1233), so true extrapolation ships later with transform rebuild for car+wheels+head (AlignWheelsAndHeadOnCar, Player_Car.c:3270) and camera handling; the confirmed unsynced-RNG sim bugs (Player_Submarine.c:608 sideForce, Player_Weapons.c landmine, Traps.c catapult/cannon/goddess) land in Stage 0 as a hard prerequisite; substitution grace is tunable 0-1 frame (default ~4ms re-poll, never a blind 250ms gate — Design 2's S_max=250ms universal freeze is rejected); Design 2's 64Hz fixed tick is rejected outright (64 divides no common refresh rate; the variable-dt host-clock model and refresh-aligned gTargetFPS pacing are KEPT). Design 3 (state streaming) is recorded as the documented endgame if internet play is ever wanted, but is not this step: 10-14 realistic weeks, client-side neutering of triggers/weapons/checkpoints/modes, puppet terrain-residency problems, and a wired remote-view REGRESSION (58ms staleness vs lockstep's exact positions). Design 2's UDP+K-window transport remains the named, data-triggered contingency: the new pump/queue API is written transport-agnostic so it drops into NetLow.c without touching game logic.

## Protocol specification
TRANSPORT: unchanged — TCP star on port 49959, existing socket options kept (NODELAY/keepalive 5s+3x1s/QUICKACK/NOTSENT_LOWAT/64KB bufs, ApplyTCPSocketOptions NetLow.c:176-235), MSG_PEEK PollSocket framing (NetLow.c:671-776), RecvAll (604-653), UDP lobby discovery, round-robin host polling (NetLow.c:783-836). ONE protocol bump at Stage 2: 4CC 'CMR6'→'CMR7' (netsprocket.h:24) AND kNSpMaxPayloadLength 256→512 (netsprocket.h:21-22) in the same change (old peers cleanly rejected by the 4CC check at NetLow.c:718-723); raw little-endian structs retained with _Static_assert(sizeof) guards on every wire struct; ALL CMR7 fields land in this single bump so Stages 3-6 change no wire bytes. Host validates playerNum/from-ID bounds on every received message before any array index (closes the wire-driven OOB writes found in review).

MESSAGES:
[H→C broadcast, one per host frame] kNetHostControlInfoMessage (extends network.h:49-63): fps,fpsFrac f32 (host dt from CalcFramesPerSecond, NEW net-only max clamp dt ≤ 2/gTargetFPS ≈ 33ms); randomSeed u32 (MyRandomLong, kept fatal check); frameCounter u32; simTick u32; per MAX_PLAYERS(6): controlBits u32, controlBitsNew u32 (host-derived), analogSteering OGLVector2D, pauseState u8, inputFlags u8 (bit0 substituted, bit1 coalesced); syncPos OGLPoint3D[6] + syncRotY f32[6] (rubber-band feed, kept); NEW ackInputSeq[MAX_CLIENTS=4] u32 = last REAL (non-substituted) input seq applied per client — each client computes true end-to-end input delay = ownSeq − ack; NEW queueDepth[MAX_CLIENTS] u8 and targetDepth[MAX_CLIENTS] u8 (telemetry); NEW event block: eventCount u8 + up to 2 × {effectiveFrame u32, type u8 (kEvBecomeBot, kEvUnpauseForce, reserved), playerNum i8, pad u16} — frame-aligned events applied by every machine when simulating frame==effectiveFrame. Payload ≈ 218B (current) + ~46B ≈ 264B < 512 cap.
[C→H, wall-clock cadence] kNetClientControlInfoMessage (replaces network.h:68-82): playerNum i16, inputSeq u32 (monotonic, client-owned), controlBits u32, analogSteering OGLVector2D, pauseState u8, lastHostFrameSeen u32 (RTT/diagnostics) ≈ 24B payload / 52B wire. DELETED from wire: controlBitsNew (host derives), prevControlBits[8]/prevAnalogSteering[8] (−96B; TCP is ordered — gaps were always counter-desync bugs, never loss).
[any↔any] kNetKeepAliveMessage 'keep' (28B header only): sent when nothing else sent for 50ms in lobby, char-select, loading barriers (HostWaitForPlayersToPrepareLevel NetHigh.c:693-701, ClientTellHostLevelIsPrepared 771-779), and menus — keeps lastHeard fresh and WiFi radios out of power-save, and warms the jitter estimator pre-race. NOT needed in-game: both directions stream continuously by construction (G1).

SEND PATH: SendOnSocket (NetLow.c:1679-1750) becomes SendOrEnqueue — one non-blocking send() attempt, remainder appended to a per-socket 32KB ring (≈2s of host msgs at ~290B wire × 60pps ≈ 17KB/s; ≈10s of client msgs at ~52B); the 100×SDL_Delay(2) retry loop is DELETED. NSpGame_FlushSends() drains rings every Net_Pump. Ring overflow (≈2s backlog) → existing kick path (NSpPlayer_Kick NetLow.c:1845) on host / fatal on client.

HOST FRAME (replaces blocking HostReceive at Main.c:1122-1125 / NetHigh.c:1179-1262): (1) Net_Pump: flush send rings; drain ALL readable messages — client inputs into the KEPT per-client queues (sHostInputQueues/Queue_Push/Peek/Pop, NetHigh.c:1096-1133, enlarged to 128 slots ≈2s), everything else (PlayerLeft, pause, etc.) to the dispatcher. (2) Host_ConsumeClientInputs, per client i with backlog B_i vs adaptive target D_i: B_i==0 → optional GRACE re-poll (tunable 0..1 frame, default two pump retries with SDL_Delay(1) ≈ 2-4ms — recovers kernel-buffered near-misses, imperceptible) then SUBSTITUTE: hold last-applied controlBits/analogSteering, controlBitsNew=0, pauseState HELD (a radio blip can never spuriously unpause), decay throttle/steer to neutral after 30 consecutive substituted frames; do NOT advance gClientSendCounter[i]; set inputFlags bit0. B_i>D_i+1 → COALESCE: pop down to D_i; iterate popped packets in seq order computing per-step edges bitsNew_k=(bits_k ^ bits_{k−1}) & bits_k and OR them into the applied frame's controlBitsNew; apply newest packet's bits/analog/pauseState; advance counter to newest+1; set bit1. Else → apply exactly one real packet, derive edges vs lastApplied, advance counter. Stale rule becomes seq ≤ lastAppliedSeq → discard (preserves NetHigh.c:1189-1193 semantics under the new bookkeeping). (3) HostSend_ControlInfoToClients (NetHigh.c:794-844 extended). (4) Simulate. (5) Render. (6) Existing gTargetFPS spin-cap kept (Main.c:1096-1111). RecoverFromFuture (NetHigh.c:1146-1164) DELETED.

CLIENT FRAME (replaces blocking ClientReceive at Main.c:1040 / NetHigh.c:923-974): (1) Net_Pump: drain host packets into a 32-slot ring (~530ms), flush sends. (2) For k=0..K_max−1 (K_max=3): if ring nonempty → consume via Client_InGame_HandleHostControlInfoMessage KEPT VERBATIM (dup-drop, fatal seed check 876-880, dt adoption 870-871, input copy 882-887, 0.2 rubber-band 889-907) → StepGameSimulation(dt from that packet) — bounded catch-up, bit-identical trajectory since each backlogged packet replays its own dt; if empty at k=0 → HOLD-LAST-FRAME (v1): render previous frame again, show subtle net-icon after 250ms (Stage 6 upgrades this to transform-rebuilt visual extrapolation, cap 100ms). (3) WALL-CLOCK INPUT SAMPLER (G1, decoupled from packet consumption): while (now ≥ nextSampleTime && burst<3): ReadKeyboard+GetLocalKeyState (Main.c:1077-1083 machinery), build packet seq=gClientSendCounter[me]++, enqueue; nextSampleTime += 1/gTargetFPS; resync nextSampleTime after >250ms gaps (app suspend). Sender runs during downlink stalls, catch-up, and the pause menu — uplink never goes silent, so a downlink spike is invisible to other players, and TX cadence doubles as the radio keepalive. Crystal drift between client and host clocks (~50ppm) is absorbed by the depth controller. Clients pace on packet availability + vsync; host keeps the spin-cap.

ADAPTIVE DEPTH (per client, host-side): ring of last 128 packet inter-arrival deltas; J_i = P95 deviation from F=1/gTargetFPS; D_i = clamp(ceil(J_i/F)+1, 1, 8). On underrun: D_i = min(8, D_i + framesSubstituted) immediately; decay 1 frame per 2s while P99 margin ≥ 1 frame. Seeding: D_init = 2 if Net_GetConnectionHint()==0 else 6 (hint KEPT, NetHigh.c:1277-1357, demoted from global mode switch to per-client seed); client pre-sends D_init duplicate input packets, REPLACING the 30-frame ~500ms pre-roll burst (Main.c:1004-1018, deleted). gUseRedundancy wire flag retired (NetConfigMessage field becomes reserved).

PAUSE: pauseState rides both messages as today; IsNetGamePaused (NetHigh.c:1551-1567) unchanged; UpdatePausedMenuCallback (Paused.c:142-165) re-pointed to pump/consume + wall-clock sender — full-rate lockstep continues during pause (counters aligned, radios awake); fix the TODO at Paused.c:156 (bail out if net game dies while menu open). JOIN: lobby-only, unchanged (UpdateNetSequence flow); mid-race join explicitly unsupported (deterministic lockstep would need full sim-state snapshots across 6 platforms — out of scope, documented). LEAVE (G3): on PlayerLeft/TCP-death, host does NOT call PlayerUnexpectedlyLeavesGame immediately; it substitutes that player's inputs and broadcasts kEvBecomeBot{playerNum, effectiveFrame=currentHostFrame+D_max+4}; EVERY machine (host included) executes the existing conversion logic (isComputer/isEliminated/pauseState-clear/gNumRealPlayers preserved, NetHigh.c:1513-1545) when its sim reaches effectiveFrame — so the ChooseTaggedPlayer synced RandomRange draw (Player.c:621) lands at the identical point in every RNG stream. Graceful ramp: silence → hold-last ≤0.5s → neutral decay → bot at drop.

TIMEOUT POLICY: 4-strike DATA_TIMEOUT (NetHigh.c:45, 958-972, 1252-1261, never-reset gTimeoutCounter) DELETED. Per-connection lastHeard (any bytes): >1s → on-screen connection badge, substitution continues; >10s → host: kEvBecomeBot path; client: kNetSequence_ErrorNoResponseFromHost. TCP keepalive (already configured 5s+3×1s) detects dead peers ~8s → existing synthesized PlayerLeft/GameTerminated (NetLow.c:797-815, 846-857) feeding the same frame-aligned conversion.

MOBILE RADIO: in-game 60pps both directions by construction; 20Hz 'keep' elsewhere; Android acquires WIFI_MODE_FULL_LOW_LATENCY WifiLock via the existing JNI block (Main.c:1736-1790, next to the MulticastLock); known limitation documented: OS backgrounding still kills the session in ~8s (keepalive death) — out of scope, future work is a 30s pause-grace.

RECOVERY SUMMARY: uplink gap → substitution (others see that car coast, hold-last; nobody stalls), burst arrival → one-frame coalesce with no lost edges, D_i bumps then decays. Downlink gap → affected client holds frame (v1) / extrapolates (v2), then catch-up at net +2 frames/render-frame; uplink unaffected (G1). Send pressure → ring buffers, overflow → kick. TCP RTO residual: ~200-300ms localized hitch, honestly retained; telemetry threshold triggers the UDP contingency (see risks).

## Latency budget
DEFINITIONS: F=1/gTargetFPS (16.7ms @60); D_i=adaptive per-client queue depth (frames); RTT LAN wired 0.2-1ms, WiFi 2-10ms typical with 50-200ms jitter spikes. OWN-INPUT DELAY (client i, hands→own screen) ≈ 0.5F (sample alignment) + D_i·F (uplink ½RTT + queue wait; valid while ½RTT < D_i·F, true on any LAN) + ½RTT (downlink) + 0.5-1F (render) — measured live as (ownSeq − ackInputSeq)·F + ½RTT via the new ack field. Host's own input ≈ 1F (unchanged). REMOTE-CAR VIEW: lockstep has ZERO positional staleness (every machine simulates identical state); what lags is the remote player's INPUT entering the shared sim = that player's own-input delay; viewer-side add ½RTT_viewer + 0.5F. This beats Design 3's state streaming on wired (which would add 40-66ms remote-car staleness lockstep simply doesn't have).

BUDGET TABLE (@60fps):
| Config | Own-car input delay | Remote-car (their hands → your screen) | Steady jitter absorbed |
| All-wired (today) | ~25-35ms (but stall-ridden) | ~35-50ms | none — any lateness = global freeze |
| All-wired (CMR7, D=1) | ≈ 8.3+16.7+0.4+12 ≈ 34-38ms (honest +0-8ms vs today) | ≈ 35-45ms | ≤16ms late packets invisible |
| All-WiFi (today) | ~520ms+ (30-frame pre-roll, Main.c:1004-1018) | ~530ms | bought with 500ms lag; spikes still freeze all |
| All-WiFi (CMR7, D=2-4) | ≈ 8.3+(33..67)+3+12 ≈ 55-90ms | ≈ 60-95ms | P95 jitter ≤ D·F (33-67ms; D_max=8→133ms) |
| Mixed (CMR7) | wired client 34-38ms; WiFi client 55-90ms — each pays only its own link | wired viewer sees WiFi car at WiFi player's D; WiFi viewer sees wired car at +D_wifi downlink only | fully per-link |

200MS-SPIKE STALL BEHAVIOR (the headline matrix):
| Event | Today (verified) | CMR7 rev B |
| WiFi client UPLINK spike 200ms | Host busy-spins (NetHigh.c:1179-1262) → no broadcast → EVERY machine freezes ~200ms; +1 never-reset strike (4 per session = fatal) | Host substitutes ~12 frames: ZERO stall on any machine; that one car coasts hold-last ~200ms on others' screens, rubber-bands back at 0.2/packet over ~10-20 packets (≤ a few % car-length error each); burst arrival coalesced in ONE frame, edges preserved via host re-derivation; D_i bumps→decays over ~8-12s |
| WiFi client DOWNLINK spike 200ms | Client busy-spins frozen (NetHigh.c:923-974), then time-dilated playback forever (1 packet/frame, no catch-up since 9caf74e); its uplink also stops → host stalls everyone | Affected client only: hold-last-frame ~200ms (v2: 100ms extrapolation + 100ms hold), then catch-up at K_max=3 (net +2/frame) clears 12 frames in 6 render frames ≈ 100ms of 3× playback; total local disturbance ~300ms; OTHER PLAYERS SEE NOTHING (wall-clock sender keeps uplink alive — G1 fix) |
| Host hiccup 200ms (GC/OS) | All freeze 200ms + one 111ms dt step (DEFAULT_FPS=9 floor) + strike | All freeze ~200ms (host is the clock — unavoidable in lockstep), but dt clamp 2/gTargetFPS → time briefly dilates instead of one violent 111ms physics step; no strike, no fatal |
| Send-buffer pressure (slow client) | SendOnSocket retry: up to 200ms/socket, ~600ms/broadcast main-thread stall (NetLow.c:1700-1746) | <0.5ms: enqueue to 32KB ring, flush next pump; overflow ≈2s backlog → kick |

STALL BUDGET: main thread never blocks on network in any role; per-frame network cost = pump + non-blocking sends <0.5ms. Visible client stalls require >250ms continuous host-packet absence (badge threshold) — essentially never on wired; on WiFi ≈ the tail of >100ms delivery gaps (a few/hour on 5GHz, more on congested 2.4GHz). TCP RESIDUAL (honest): 802.11 MAC retries convert most radio loss into delay (absorbed by D_i); with continuous 60pps streams, fast retransmit recovers in ≈RTT+50ms (invisible); a true RTO yields one ~200-300ms localized hitch — expected <1/10min on 5GHz, a few/hour on bad 2.4GHz; this is the one class only the UDP contingency removes. BUFFERS: host per-client input ring 128 slots (~2s); client host-packet ring 32 (~530ms); send rings 32KB/socket (host ≈2s at 290B×60pps≈17KB/s — judge-corrected arithmetic; client ≈10s at 52B); badge 1s; drop 10s. BANDWIDTH: host TX ≈ 290B×60×3 ≈ 52KB/s total; client TX ≈ 52B×60 ≈ 3.1KB/s — trivial; 60pps bidirectional per link is also the radio-keepalive.

## Mixed wired/WiFi LAN
Mixed wired+WiFi is the design's headline win and emerges from three independent mechanisms rather than configuration: (1) PER-CLIENT ADAPTIVE DEPTH — each client's D_i is sized from its OWN measured arrival jitter (P95 of last 128 inter-arrivals), seeded by its own Net_GetConnectionHint (carried per-client in NetPlayerCharTypeMessage.connectionType, network.h:95): the wired client converges to D=1 (34-38ms own-input delay, indistinguishable from an all-wired session), the WiFi client to D=2-4 (55-90ms), spiking to 8 (133ms) after a bad burst and decaying back at 1 frame/2s. This replaces today's all-or-nothing gUseRedundancy flag where ONE WiFi peer drags EVERY client into redundancy+500ms pre-roll (NetHigh.c:389-394). (2) HOST NEVER WAITS — substitution replaces the HostReceive busy-spin (NetHigh.c:1179-1262), and send rings replace the SendOnSocket retry stall (NetLow.c:1700-1746), so a WiFi client's radio behavior is fully decoupled from the host frame clock: a 200ms WiFi spike no longer freezes the host and therefore no longer freezes the wired client. (3) WALL-CLOCK CLIENT SENDER (G1) — a WiFi client's downlink spike no longer silences its uplink, so the residual cross-coupling Design 1 had (downlink spike → host substitutes → car coasts on everyone's screen) is eliminated; the ONLY remaining cross-visibility is during a genuine UPLINK spike, when that one car coasts on hold-last inputs and is pulled back by the kept 0.2 rubber-band — wired players' input latency, frame pacing, and smoothness remain bit-for-bit the all-wired experience. HOST PLACEMENT: a WiFi host puts the radio on every path; substitution and per-client depth still prevent freezes, but all clients' D_i inflate — the lobby UI gains a "host on the wired machine" hint (idea adopted from Design 3). REFRESH-RATE MIXING: gTargetFPS LCD negotiation KEPT with its asymmetry bug fixed (today each machine lowers only on RECEIVED char-type broadcasts, NetHigh.c:382-394, so machines can end with different caps) — the host computes the final value after vehicle select and broadcasts it once in the level-prep kNetSyncMessage (network.h:41-44 gains a targetFPS field in the CMR7 bump); all machines then pace at the same negotiated rate, host via the kept spin-cap (Main.c:1096-1111), clients on packet availability + vsync, with hold-last (v1) / extrapolation (v2) covering sub-frame gaps on higher-refresh displays. No fixed tick is introduced, so there is no tick-vs-vsync beat judder (the verified flaw in Design 2's 64Hz proposal).

## Determinism & fidelity impact
DETERMINISM: preserved by construction, then HARDENED. Clients only ever simulate the host echo (Main.c:1075-1083 overwritten at NetHigh.c:882-887 before sim), so substitution/coalescing changes WHICH inputs enter the shared sim, never WHETHER machines agree; the per-frame fatal RNG seed check (randomSeed=MyRandomLong at NetHigh.c:812, verified at 876-880) is kept untouched and remains valid. Three hardening fixes the base design lacked: (a) frame-aligned bot conversion (G3) removes the leave-time desync where ChooseTaggedPlayer's synced RandomRange draw (Player.c:621 via NetHigh.c:1542) and isComputer-gated AI (Player_Car.c:513+) could fire on different frames per machine; (b) Stage 0 lands the confirmed unsynced-RNG sim bugs — Player_Submarine.c:608 (VisualRandomLong steering side-force in a replicated sim path), the landmine spin in Player_Weapons.c, and the catapult/cannon/goddess draws in Traps.c — converting latent divergence into none (free-running catch-up bursts would have exercised these harder); (c) recommend -ffp-contract=off across all 6 targets so ARM FMA cannot change a branch that changes synced-RNG draw count (positions drifting is absorbed by rubber-band; draw-count changes are fatal). PHYSICS/FEEL: the original variable-timestep model is fully preserved — host CalcFramesPerSecond (Misc.c:430-452) with its DEFAULT_FPS=9 floor still dictates dt for every machine; the ONLY dt change is a net-only max clamp of 2/gTargetFPS (~33ms) so a host hiccup dilates time briefly instead of producing one 111ms physics step (bounds rubber-band correction; arguably more faithful to feel). No fixed tick, no render/tick beat, single-player and split-screen untouched. Catch-up replays each backlogged packet with its own dt → bit-identical state trajectory to uninterrupted play. EXISTING MACHINERY DISPOSITION: rubber-band (0.2/packet incl. own car, NetHigh.c:889-907) KEPT EXACTLY AS-IS — do not deadband in v1 (Design 2's deadband would re-expose latent cross-arch FP drift the constant pull currently masks); note the WiFi self-drag artifact shrinks 5-10× automatically because host-authoritative position now trails the client by 33-133ms instead of ~500ms. Seed check KEPT fatal (desync error message extended with simTick + per-client ack seqs for diagnosability). Pre-roll DELETED (replaced by D_init duplicate packets: 2 wired / 6 WiFi). 8-frame wire redundancy + RecoverFromFuture DELETED (provably dead weight on ordered TCP; its edge/pause zeroing at NetHigh.c:1157-1160 was strictly harmful) — K-window redundancy returns only with the UDP contingency, where it becomes genuinely useful. Dead Client_CheckIfMorePacketsWaiting (NetHigh.c:982-1023, network.h:112) DELETED. DETERMINISTIC-AI QUESTION, answered explicitly: AI stays REPLICATED AND DETERMINISTIC (no host-only AI, no streaming) — it is what keeps the wire format 52B/290B and remote positions exact; the cost is the Stage 0 RNG hygiene + frame-aligned events above, paid once. HONEST BEHAVIORAL DELTAS: (1) during an uplink spike a remote car coasts ~200ms then rubber-bands (today: everyone freezes — a global stall traded for a local trajectory smudge); (2) two presses of the same button within a coalesced window (≤50-130ms) can merge into one frame's edge — strictly better than today's silent edge drop, and host-side per-step derivation (G2) preserves every press across coalescing; (3) substituted inputs could in principle decide a photo-finish — inputFlags bit0 makes it auditable; (4) inputs uttered during a >D_max outage are lost (unavoidable in any non-rollback design).

## Staged implementation plan
Each stage compiles, ships, and is soak-testable alone. ~80% of the diff is in Source/Network/NetHigh.c, Source/Network/NetLow.c, Source/Headers/network.h, Source/Headers/netsprocket.h, and PlayArea in Source/System/Main.c.

STAGE 0 — hygiene + determinism prerequisites + test scaffolding (no wire change, shippable as 3.0.2 patch, 3-4d): (a) sync unsynced sim-RNG: Player_Submarine.c:608 sideForce, Player_Weapons.c landmine spin, Traps.c catapult/cannon/goddess → ChaoticFloat keyed on gSimulationFrame (pattern Misc.c:238-253) or synced RandomFloat; leave purely visual draws (Items.c particles) alone; (b) reset sHostInputQueues (NetHigh.c:1103), gTimeoutCounter, ClientSend statics historyControlBits/historyAnalog (NetHigh.c:1036-1037) in EndNetworkGame (NetHigh.c:188-224) — fixes the second-hosted-game wedge; (c) fix sync-mask playerNum/NSpPlayerID conflation (NetHigh.c:1182-1210 vs NSpGame_GetActivePlayersIDMask via NetLow.c:1425-1451); (d) fix OOB scan NetLow.c:472 (MAX_PLAYERS→MAX_CLIENTS); (e) bounds-validate playerNum on all received messages (Queue_Push/ApplyClientMessage/char-type handler); (f) non-blocking connect() in JoinLobby (NetLow.c:406-419); (g) add SDL_Delay(1)+SDL event pump inside both existing busy-wait loops (NetHigh.c:923-974, 1179-1262) — kills 100%-CPU spin and frozen-window "not responding" with zero protocol impact; (h) BUILD THE TEST HARNESS NOW (see TEST PLAN) + debug HUD scaffolding (F3-style overlay) + CSV telemetry logger.

STAGE 1 — non-blocking sends (no wire change, biggest stall-removal per line of code, 2-3d): NetLow.c: add 32KB SendRing per NSpPlayer (struct at NetLow.c:73-90) + one for clientToHostSocket; rewrite SendOnSocket (1679-1750) as SendOrEnqueue (one send(), remainder to ring, DELETE the 100×SDL_Delay(2) loop); NSpGame_FlushSends() from a new Net_Pump called each frame; overflow → existing NSpPlayer_Kick (1845). The broadcast loop (NSpMessage_Send 1778-1798) becomes stall-free automatically. Removes the verified 200-600ms/frame host stall class. Validate: netem profile C, send-ring high-water metric.

STAGE 2 — CMR7 bump + host-never-waits + adaptive depth + wall-clock sender + edge re-derivation (THE stutter win, 5-7d): bump 'CMR6'→'CMR7' + payload cap 512 (netsprocket.h:21-24); rewrite network.h structs per protocolSpec with _Static_asserts; split HostReceive_ControlInfoFromClients (NetHigh.c:1166-1263) into Host_PumpClientInputs (non-blocking drain into kept sHostInputQueues, enlarged 128 slots) + Host_ConsumeClientInputs (depth controller; substitution with sLastApplied[MAX_PLAYERS]; coalesce with per-step edge derivation using the InputControlBits.c:145 formula; counter bookkeeping per protocolSpec); DELETE RecoverFromFuture (1146-1164), client history block (1043-1059, 1077-1084), host 4-strike block (1252-1261); Main.c: delete end-of-frame HostReceive call (1122-1125), host top-of-frame = Net_Pump → Host_ConsumeClientInputs → HostSend (extended, NetHigh.c:794-844); replace pre-roll burst (Main.c:1004-1018) with D_init duplicates; implement the client wall-clock sampler (G1) by refactoring the Step-3 block (Main.c:1075-1084) into SampleAndSendLocalInput() driven by nextSampleTime; client receive still consumes 1 packet/frame at this stage (busy loop retained from Stage 0g but now with badge UI). HOST-SIDE WIN COMPLETE: host never freezes regardless of any client's radio. Validate: profiles B-D, substitution/coalesce/D_i metrics, 2-instance localhost + 3-netns mixed run.

STAGE 3 — client free-running + bounded catch-up + hold-last (4-5d): replace ClientReceive_ControlInfoFromHost busy loop (NetHigh.c:923-974) with Client_PumpHostPackets (32-slot ring) + consume loop K_max=3; KEEP Client_InGame_HandleHostControlInfoMessage (853-911) verbatim; factor the sim block (Main.c:1052-1069) into StepGameSimulation(); empty-ring → hold-last-frame + badge ≥250ms; DELETE dead Client_CheckIfMorePacketsWaiting (982-1023, network.h:112). Profile K_max on weakest Android target EARLY — StepGameSimulation includes DoPlayerTerrainUpdate, so 3× supertile streaming per render frame is the real cost (fallback K_max=2). Validate: downlink-spike profile → confirm other-machine invisibility (G1) and catch-up ≤300ms.

STAGE 4 — policy: timeouts, pause, leave, negotiation, mobile (3-4d): lastHeard badge/drop policy replacing DATA_TIMEOUT (NetHigh.c:45); frame-aligned kEvBecomeBot path wrapping PlayerUnexpectedlyLeavesGame (1513-1545) per protocolSpec (G3) — explicit tests for tag-mode leave and leave-while-paused; UpdatePausedMenuCallback (Paused.c:142-165) → pump/consume + sampler, fix Paused.c:156 TODO; 'keep' heartbeat in barriers (NetHigh.c:693-701, 771-779) and lobby; gTargetFPS finalize-and-broadcast in level-prep kNetSyncMessage (fixing NetHigh.c:382-394 asymmetry); Android WIFI_MODE_FULL_LOW_LATENCY WifiLock in the JNI block (Main.c:1736-1790).

STAGE 5 — cross-platform soak + tuning (3-5d): full-race x86-host/ARM-client seed-check soak (catch-up bursts exercise FP paths hardest); D_i controller tuning against profile D; pre-agreed UDP-swap threshold evaluation (see risks).

STAGE 6 — visual extrapolation done right (optional polish, 3-4d): new Source/Network/NetSmooth.c — on empty ring, save the 6 cars' Coord/Rot.y, advance by ObjNode Delta × elapsed (cap 100ms), call UpdateObjectTransforms (Objects.c:1233) on car body AND chained wheel/head nodes via AlignWheelsAndHeadOnCar (Player_Car.c:3270), freeze camera (UpdateCamera lives in the skipped MoveEverything — audit), render, restore Coord/Rot.y AND matrices. Audit render-path consumers of car Coord (shadows, skid marks, particles). Ship only if hold-last feels insufficient in Stage 5 playtests.

TEST PLAN (single machine, built in Stage 0, run every stage): HARNESS: three network namespaces (ip netns add cmr_h/cmr_c1/cmr_c2) joined by veth pairs to a bridge; run the game per-namespace via existing CLI --host / --join <addr> / --port (verified working, Main.c:1823-1842; gCommandLine.netHost/netJoin); impair per-veth with tc qdisc netem. PROFILES: A=clean (0ms, baseline determinism); B=WiFi-typical (delay 3ms jitter 2ms distribution pareto, loss 0.1%); C=spiky 5GHz (B + scripted 200ms delay bursts every 20-60s via tc qdisc change toggling, loss 0.5%); D=congested 2.4GHz (delay 10ms jitter 30ms, periodic 50-200ms bursts, loss 1-3%, rate 20mbit); E=asymmetric mixed (c1 clean, c2 profile D) — THE acceptance profile. AUTOMATION: bot-driven races (all-CPU sim still exercises the full lockstep wire), scripted N-race soaks asserting zero kNetSequence_SeedDesync / zero spurious fatals / second-game-in-process start. METRICS (CSV + debug HUD, per second): per-client D_i, queue depth, substitutions/min, multi-frame substitution events, coalesce events, max delivery gap ms, RTO-shaped gaps (>150ms), send-ring high-water bytes, hold/extrapolation events + durations, ack input delay (ownSeq−ack)·F, frame-time histogram p50/p95/p99, seed-check pass count. ACCEPTANCE: profile E → wired client frame-time p99 ≤ 1.2F and own-input delay ≤40ms while c2 runs profile D; no global stall >50ms attributable to c2; profile C → ≤1 visible hitch (>100ms hold) per 10min; profile A → 20-race soak, zero desync, zero fatals. CROSS-ARCH: one wired x86 box + one ARM device (Android or Apple Silicon) repeating profile A/B soaks.

## Risks & mitigations
(1) TCP head-of-line on a genuine RTO remains: one ~200-300ms localized hitch on the affected path; expected <1/10min on 5GHz, a few/hour on congested 2.4GHz. MITIGATION + EXIT CRITERION (pre-agreed): the Stage 0 telemetry logs RTO-shaped gaps and multi-frame substitution events; if field/soak data shows >2 multi-frame substitution events/min on a healthy 5GHz link, execute the named contingency — Design 2's UDP transport + K=10 redundancy window swapped inside NetLow.c behind the transport-agnostic pump/queue API (game logic untouched; the deleted redundancy concept returns where it actually works). (2) Coalescing merges same-button double-taps within ≤50-130ms into one edge (inherent to one-edge-per-frame); host-side per-step derivation minimizes it and it strictly improves on today's silent edge drop — add explicit unit tests for pause-edge ordering across coalesced packets. (3) Wall-clock sampler is new machinery; a bug manifests as host-side input gaps — keep a build flag reverting to send-per-consumed-packet for A/B testing; resync nextSampleTime after suspend to avoid burst floods (cap 3/frame). (4) Frame-aligned kEvBecomeBot interacts with pause, race-end, and double-leave — needs state-machine tests (leave while paused, leave of the tagged player, two leaves same frame, leave during loading barrier); host must keep substituting between TCP-leave arrival and effectiveFrame. (5) K_max=3 catch-up cost on weakest Android (terrain supertile streaming in StepGameSimulation) may exceed frame budget — profile in Stage 3, fallback K_max=2 (slower but complete recovery). (6) Cross-arch float divergence (x86 vs ARM FMA) is unchanged from shipping but exercised harder by catch-up bursts; rubber-band absorbs position drift, seed check only guards draw count — pin -ffp-contract=off, soak x86↔ARM; do NOT deadband the rubber-band (Design 2's mistake). (7) Wire-ABI: changed structs must have identical layout on all 6 platforms — _Static_asserts on every wire struct + the 4CC bump cleanly rejecting CMR6 peers (NetLow.c:718-723); do the payload-cap bump (256→512) in the same commit as the 4CC. (8) Hold-last-frame v1 means a frozen image during >1-frame downlink gaps (no extrapolation) — accepted for v1 since today's behavior is identical-but-global; Stage 6 upgrade path specified with the BaseTransformMatrix/camera mechanism that Design 1 originally got wrong. (9) Paused.c and the loading barriers MUST convert in the same release as Stage 3 or blocking waits return through the back door (tracked as Stage 4 hard dependency). (10) Substituted inputs near start-light/photo-finish can matter competitively — inputFlags bit0 is broadcast so a future UI can surface it; hold-last is the least-surprising policy. (11) gTimeoutCounter-class regressions: new lastHeard policy must be reset in EndNetworkGame alongside the Stage 0 resets — add a single ResetNetGameTransientState() called from both Init and End paths to make stale-state bugs structurally impossible. (12) iOS local-network permission flow is already broken in the fork (host errors on first prompt) — unrelated to this redesign but will block WiFi testing on iOS; schedule the known fix before Stage 5 device soaks.

## Effort estimate
20-30 dev-days (~4-6 calendar weeks, one developer who knows the codebase). Stage 0 hygiene+RNG fixes+harness 3-4d; Stage 1 send rings 2-3d; Stage 2 CMR7 host-never-waits + adaptive depth + wall-clock sender + edge re-derivation 5-7d (the single biggest stutter win; independently shippable); Stage 3 client free-running/catch-up/hold-last 4-5d; Stage 4 timeouts/pause/frame-aligned leave/negotiation/mobile 3-4d; Stage 5 cross-arch soak + tuning 3-5d; Stage 6 visual extrapolation (optional) 3-4d. Deltas vs Design 1's 15-25d estimate: +2-3d for the three grafts (wall-clock sampler, frame-aligned leave, Stage 0 RNG sync fixes — all judge-mandated), −3-4d for deferring extrapolation to optional Stage 6 and dropping client-side controlBitsNew. No new threads, no transport change, no engine-core changes; UDP contingency (if telemetry triggers it) is a further ~1-2 weeks confined to NetLow.c behind the pump API. For comparison: Design 2 standalone was realistically 5-7 weeks with worse frame pacing; Design 3 was 10-14 weeks with gameplay-wide regression surface — kept as the documented endgame only if internet (non-LAN) play is ever wanted.

---

# Implementation guide (per stage)

Detailed, build-ready specs for each stage, expanded from the plan above. Stages are
ordered by dependency; each is independently shippable. Anchors are grounded in the
current source. (Generated from the per-stage spec workflow.)


## Stage 0 — Hygiene, determinism prerequisites, and the netns/netem + HUD/CSV test scaffolding

**Effort:** 3-4 dev-days, independently shippable as a 3.0.2 patch (no wire change, still 'CMR6'). Split: remaining RNG conversions + per-site collidability audit ~0.75d; reset/sync-mask/bounds/OOB/connect/busy-wait correctness fixes ~1d; CLI-flag restore + direct-connect-by-address ~0.5d; netns/netem harness + profiles + soak automation ~1d; debug HUD + CSV telemetry plumbing ~0.5d. The physics, oil-scale RNG, cactus-spin, and spline-pause items are already merged on fix/review-hardening, putting this at the low end of the synthesis 3-4d estimate; the harness is the long pole and pays back across every later stage.

**Objective.** Make the existing TCP input-lockstep correct, crash-proof, and instrumentable BEFORE any wire/architecture change, and stand up the single-machine test harness every later stage validates against. No wire-format change (still 'CMR6'), shippable alone as a 3.0.2 patch. Concretely: (1) finish the sim-RNG determinism cleanup the prior commits deliberately deferred, converting the remaining position/collision-gated draws to the STATELESS ChaoticFloat helper (NOT synced RandomFloat) so per-machine divergence is removed WITHOUT adding draws to the fatal seed-check stream; (2) close the wire-reachable playerNum OOB writes and the AcceptNewClient OOB scan; (3) kill the per-process session-killers (stale sHostInputQueues, never-reset gTimeoutCounter, never-reset ClientSend history) via one ResetNetGameTransientState() wired into both init and teardown; (4) fix the sync-mask playerNum/NSpPlayerID conflation that wedges races after lobby churn; (5) make the join connect() and the two zero-sleep busy-wait barriers non-pathological (non-blocking connect, SDL_Delay(1)+event pump); (6) restore the dead --host/--join/--port CLI flags (hard prerequisite: the harness cannot launch instances without them) and build the netns/netem harness, the in-game net debug HUD, and the CSV telemetry logger.


**Already done on this branch:**
- Physics: 1999 surface-physics restore + Mammoth water-softlock fix at its source (commit 5478c84 on fix/review-hardening)
- Oil-slick scale RNG re-synced to the synced stream (Player_Weapons.c:629 VisualRandomFloat->RandomFloat) - the one frame-deterministic, input-driven draw safe to put on the synced stream (commit 89f13c9)
- Cactus car-spin centered: restored the -0.5 ChaoticFloat offset dropped in 0e4c042 so the yaw/roll kick is symmetric (Triggers.c:897-905, commit 89f13c9); ChaoticFloat is stateless so this never touched the seed stream
- Spline-pause handling already implemented on the branch (per task brief)


**Changes:**

- `Source/Player/Player_Submarine.c` (Player_Submarine.c:608 (stuck-evasion sideForce, MoveSubmarine)) — PENDING RNG. sideForce[player] = (VisualRandomLong() & 1) ? 1 : -1 feeds analogSteering.x (sim path -> car position). Replace with ChaoticFloat: sideForce[player] = (ChaoticFloat(stuckTimer[player], player) < 0.5f) ? 1.0f : -1.0f. Stateless: no synced-stream draw, cannot change seed-check draw count; removes the per-machine left/right divergence VisualRandomLong guaranteed.
- `Source/Player/Player_Weapons.c` (DoTrig_LandMine, Player_Weapons.c:1860-1861) — PENDING RNG. Landmine car-spin DeltaRot.y/z = VisualRandomFloat2()*20/*10 (sim: DeltaRot integrates into car Rot/heading). Convert exactly like the already-fixed cactus: DeltaRot.y = (ChaoticFloat(whoNode->Speed3D, whoNode->PlayerNum)-0.5f)*2.0f*20.0f; DeltaRot.z = (ChaoticFloat(whoNode->Speed3D, whoNode->PlayerNum+10)-0.5f)*2.0f*10.0f.
- `Source/Items/Traps.c` (catapult throw Traps.c:727-732; cannon throw Traps.c:1191-1194) — PENDING RNG. Thrown-object launch speed/heading/Delta.y use VisualRandomFloat/Float2 -> projectile trajectory is collision-relevant (can hit cars). Convert the speed, the Rot.y jitter, and the Delta.y bump to ChaoticFloat keyed on the launcher node (e.g. ChaoticFloat(theNode->Rot.y, slot) and a distinct modifier per draw). Keep visual-only smoke/particle draws (253-290, 877-888) on VisualRandom.
- `Source/Items/Traps.c` (goddess bolt targeting Traps.c:948-950 (duplicate at 1493-1495)) — PENDING RNG. boltVector targeting = player coord + VisualRandomFloat2()*300 decides WHERE the bolt strikes (damage gating) -> sim-affecting. Convert the two targeting offsets to ChaoticFloat(gPlayerInfo[playerNum].coord.x, playerNum) / (..z, playerNum+5). Leave the bolt-path jaggedness endpoints (972-974, 1517-1519) on VisualRandom (pure visual).
- `Source/Items/Items.c` (stump spawn Items.c:838) — PENDING RNG. .type = Stump1 + (VisualRandomLong() & 0x3) sets which stump mesh -> collision geometry differs per machine. IF stumps are collidable, convert the type selector to ((uint32_t)(ChaoticFloat(coord.x,(int)coord.z)*4.0f) & 0x3); leave the .rot at 843 (visual). Verify collidability first; if purely decorative, leave as-is and note in commit.
- `Source/Items/Triggers.c` (team-torch placement Triggers.c:1441-1442) — PENDING RNG. Torch Coord.x/z += VisualRandomFloat2()*100 sets captured-torch world position (capture/scoring geometry) -> sim-affecting. Convert to (ChaoticFloat(theNode->Coord.x, slot)-0.5f)*2.0f*100 and (..Coord.z, slot+1 ..).
- `Source/Network/NetHigh.c` (new ResetNetGameTransientState() near EndNetworkGame (NetHigh.c:188)) — PENDING N. Add one function zeroing ALL net per-session transient state: memset(sHostInputQueues,0,..); gTimeoutCounter=0; gHostSendCounter=0; memset(gClientSendCounter,0,..); ClearPlayerSyncMask(); plus the promoted ClientSend history and gNetTelemetry. Risk-11: makes stale-state bugs structurally impossible.
- `Source/Network/NetHigh.c` (EndNetworkGame, NetHigh.c:212-223) — PENDING N. Replace the partial manual resets (only gHostSendCounter/gClientSendCounter at 222-223) with a call to ResetNetGameTransientState(). Fixes the 'every 2nd hosted game in-process wedges' bug (stale sHostInputQueues with future frameCounters never popped) and the 'host gTimeoutCounter never reset -> 4 cumulative stalls per process = fatal' bug.
- `Source/Network/NetHigh.c` (SetupNetworkHosting NetHigh.c:487 and SetupNetworkJoin NetHigh.c:538) — PENDING N. Call ResetNetGameTransientState() at the top of both setup paths so a session always starts from clean transient state.
- `Source/Network/NetHigh.c` (ClientSend_ControlInfoToHost statics, NetHigh.c:1036-1037) — PENDING N. Promote static historyControlBits[8]/historyAnalog[8] from function-scope to file-scope statics (sClientHistoryControlBits/sClientHistoryAnalog) so the reset can zero them; they currently survive across net games and corrupt the next session's redundancy stream.
- `Source/Network/NetHigh.c` (HostReceive_ControlInfoFromClients, NetHigh.c:1182-1210) — PENDING N (sync-mask conflation). Loop marks/checks the mask by playerNum i (PlayerIsSynced(i) 1184, MarkPlayerSynced(i) 1200/1210) but AreAllPlayersSynced (115-120) compares vs NSpGame_GetActivePlayersIDMask = NSp slot IDs. After lobby churn (IDs {0,2}, playerNums {0,1}) masks never match -> 8s timeouts then fatal. Fix: drive the mask by NSp ID. Add PlayerNumToNSpID(i)=gPlayerInfo[i].net.nspPlayerID; skip bots (isComputer); use PlayerIsSynced/MarkPlayerSynced(PlayerNumToNSpID(i)). Queue/counter indexing stays playerNum-based; char-type handler at 396 already uses message->from (NSp ID) and stays correct.
- `Source/Network/NetHigh.c` (Queue_Push 1107, ApplyClientMessage 1136, RecoverFromFuture 1148, HostReceive 1230, char-type handler 374-378) — PENDING W (wire-driven OOB). Every site indexing by wire-supplied playerNum is an attacker OOB write. Add IsValidNetPlayerNum(p){return p>=0 && p<MAX_PLAYERS;}. In HostReceive's kNetClientControlInfoMessage branch (1228-1238) reject+Release if !IsValidNetPlayerNum(cMsg->playerNum) OR gPlayerInfo[playerNum].net.nspPlayerID != inMess->from (anti-spoof) BEFORE Queue_Push. Guard ApplyClientMessage/RecoverFromFuture too. In the char-type handler replace 'TODO: Check player num' (376) with the bounds check before writing gPlayerInfo[mess->playerNum].
- `Source/Network/NetLow.c` (NSpGame_AcceptNewClient, NetLow.c:472) — PENDING W (OOB scan). for(i=0;i<MAX_PLAYERS;i++) reads game->players[i].state but players[] is MAX_CLIENTS(4) sized, MAX_PLAYERS=6 -> reads players[4]/[5] OOB; garbage==kNSpPlayerState_Offline(0) accepts a 5th client written OOB (heap corruption). Change bound to MAX_CLIENTS.
- `Source/Network/NetLow.c` (JoinLobby, NetLow.c:406-419) — PENDING (f). connect() runs on a still-blocking socket -> joining blocks the main thread for the full OS connect timeout (frozen window). Reorder: MakeSocketNonBlocking BEFORE connect; accept EINPROGRESS/EWOULDBLOCK; poll()/select() for writability with a ~3s bounded timeout while calling DoSDLMaintenance() each iteration; on writable check SO_ERROR via getsockopt; real error or timeout => fail. Fix the misleading 'make it blocking AFTER connecting' comment.
- `Source/Network/NetHigh.c` (ClientReceive_ControlInfoFromHost busy loop, NetHigh.c:923-974 (else branch 949)) — PENDING (g). Zero-sleep spin = 100% CPU + frozen window during packet lateness. In the no-message branch add DoSDLMaintenance() then SDL_Delay(1). Pure CPU/UX, zero protocol impact; Stage 3 replaces the loop.
- `Source/Network/NetHigh.c` (HostReceive_ControlInfoFromClients busy loop, NetHigh.c:1179-1262) — PENDING (g). Same spin. After the poll-network step, when inMess==NULL and not all synced, call DoSDLMaintenance() + SDL_Delay(1) before next iteration. Zero protocol impact; Stage 2 replaces the loop.
- `Source/Boot.cpp` (ParseCommandLine, Boot.cpp:94-128) — PENDING (h)/task#11. --host/--join/--port are DEAD: gCommandLine.netHost/netJoin are READ (Main.c:1823/1835, SelectVehicle.c:79) but NOTHING in ParseCommandLine sets them (lost in merge a5f48a9). Add: '--host'->netHost=true; '--join'->netJoin=true, and if argv[i+1] exists and isn't a '--' flag copy it to netJoinAddress, i+=1; '--port'->netPort=atoi(argv[i+1]), i+=1. Hard prerequisite for the netns harness.
- `Source/Headers/file.h` (CommandLineOptions struct, file.h:113-123) — PENDING (h). Add 'char netJoinAddress[64];' and 'int netPort;'. Already zeroed by SDL_memset at Boot.cpp:91.
- `Source/System/Main.c` (net startup block, Main.c:1823-1842) — PENDING (h). Before SetupNetworkHosting()/SetupNetworkJoin(), if gCommandLine.netPort>0 set gNetPort=gCommandLine.netPort. For --join with an explicit netJoinAddress call the new direct-connect path; empty address falls back to UDP discovery.
- `Source/Network/NetLow.c` (new JoinLobbyByAddress() near JoinLobby NetLow.c:386; gNetPort NetLow.c:36) — PENDING (h). Add a helper building a LobbyInfo from a dotted-quad + gNetPort and calling JoinLobby() directly, bypassing NSpSearch UDP discovery; declare gNetPort in a header so --port works. Makes the 3-namespace harness reliable even if bridge broadcast is flaky.
- `Source/3D/OGL_Support.c` (UpdateDebugText, OGL_Support.c:1304-1342 (already prints NET/SYNC lines)) — PENDING (h) HUD. Extend the existing gDebugMode debug-text overlay (the F3-style hook) with net telemetry from gNetTelemetry: per-client queue depth (sHostInputQueues head/tail delta), gTimeoutCounter, substitutions/min (0 in Stage 0), max delivery-gap ms, send-ring high-water (0 until Stage 1), ack delay (0 until Stage 2), seed-check pass count. Stage 0 wires plumbing + available fields; later stages fill the rest. Gate behind gDebugMode>=2 or a toggle key.
- `Source/Network/NetHigh.c` (new gNetTelemetry global + NetTelem_Tick CSV logger, top of NetHigh.c) — PENDING (h). Add the NetTelemetry struct (below) updated each host/client frame, plus a once-per-second CSV append (path from env CMR_NET_CSV) emitting the METRICS row set. Counters reset in ResetNetGameTransientState(). This is the soak evidence source for every later stage and the risk-1 UDP-contingency trigger.
- `CMakeLists / per-target build flags` (fidelityImpact(c)) — PENDING (optional, recommended). Add -ffp-contract=off to all 6 targets so ARM FMA cannot change a synced-RNG-gated branch (draw-count change = fatal seed desync). Build-config only, no source change.
- `test/netem/ (new, not shipped)` (cmr_harness.sh + profiles A-E) — PENDING (h). netns/netem harness: ip netns add cmr_h/cmr_c1/cmr_c2 joined by veth pairs to a bridge; launch per-namespace with --host / --join <hostaddr> --port; tc qdisc netem per-veth for profiles A/B/C/D/E (E = acceptance); bot-driven N-race soak asserting zero kNetSequence_SeedDesync / zero spurious fatals / clean second-game-in-process start; parse CSV for thresholds.


**Data structures.** /* file.h: CommandLineOptions additions (CLI restore) */
typedef struct {
    int   vsync, bootToTrack, car;
    bool  netHost;
    bool  netJoin;
    char  netJoinAddress[64]; /* NEW: dotted-quad for --join <addr>; "" => UDP discovery */
    int   netPort;            /* NEW: --port; 0 => default gNetPort (49959) */
    int   display, windowedWidth, windowedHeight;
} CommandLineOptions;

/* NetHigh.c: promote ClientSend history out of function scope so it is resettable */
static uint32_t    sClientHistoryControlBits[8];   /* was function-static in ClientSend_ControlInfoToHost (NetHigh.c:1036-1037) */
static OGLVector2D sClientHistoryAnalog[8];

/* NetHigh.c: telemetry collected each frame, emitted to HUD + CSV, reset per session */
typedef struct {
    uint8_t  queueDepth[MAX_PLAYERS];      /* live sHostInputQueues occupancy */
    uint32_t substitutions[MAX_PLAYERS];   /* 0 in Stage 0; populated Stage 2 */
    uint32_t coalesceEvents[MAX_PLAYERS];  /* 0 in Stage 0; populated Stage 2 */
    uint32_t maxDeliveryGapMs;             /* max inter-arrival gap this second */
    uint32_t rtoShapedGaps;                /* count of gaps > 150ms */
    uint32_t seedCheckPasses;              /* ++ at NetHigh.c:876 on match */
    uint32_t timeoutStrikes;               /* mirror of gTimeoutCounter for the HUD */
    uint32_t frameTimeUs[256]; uint32_t frameTimeCount; /* histogram -> p50/p95/p99 at emit */
} NetTelemetry;
static NetTelemetry gNetTelemetry;

/* NetHigh.c: single reset entry point (risk-11), also declared in network.h */
void ResetNetGameTransientState(void);

/* network.h: make the port settable for --port */
extern int gNetPort;                     /* defined NetLow.c:36 */


**Algorithms.** ResetNetGameTransientState():  /* called from SetupNetworkHosting, SetupNetworkJoin, EndNetworkGame */
  memset(sHostInputQueues, 0, sizeof sHostInputQueues);   /* head=tail=0 => empty for all */
  memset(sClientHistoryControlBits, 0, ...); memset(sClientHistoryAnalog, 0, ...);
  gTimeoutCounter = 0; gHostSendCounter = 0;
  memset(gClientSendCounter, 0, sizeof gClientSendCounter);
  ClearPlayerSyncMask();
  memset(&gNetTelemetry, 0, sizeof gNetTelemetry);

Sync-mask identity unification (HostReceive_ControlInfoFromClients):
  PlayerNumToNSpID(i) = gPlayerInfo[i].net.nspPlayerID
  for i in 0..gNumRealPlayers-1:
      if gPlayerInfo[i].isComputer: continue          /* bots have no NSp ID, never in target mask */
      id = PlayerNumToNSpID(i)
      if PlayerIsSynced(id): continue
      ... process sHostInputQueues[i]/gClientSendCounter[i] (unchanged, playerNum-indexed) ...
      on apply/recover: MarkPlayerSynced(id)          /* was MarkPlayerSynced(i) */
  /* AreAllPlayersSynced() keeps comparing vs NSpGame_GetActivePlayersIDMask (NSp-ID space) -> now consistent */

Wire bounds + anti-spoof (host inbound client control message, NetHigh.c:1228):
  p = cMsg->playerNum
  if !IsValidNetPlayerNum(p):                          drop+Release; continue
  if gPlayerInfo[p].net.nspPlayerID != inMess->from:   drop+Release; continue   /* spoofed slot */
  Queue_Push(p, cMsg)
  /* same IsValidNetPlayerNum guard at top of ApplyClientMessage, RecoverFromFuture, char-type handler */

Non-blocking connect (JoinLobby):
  MakeSocketNonBlocking(sockfd)
  rc = connect(sockfd, addr)
  if rc==0: connected
  elif errno in {EINPROGRESS, EWOULDBLOCK}:
      deadline = now + 3s
      loop: DoSDLMaintenance(); poll(sockfd, POLLOUT, 100ms)
            if writable: getsockopt(SO_ERROR)->err; if err goto fail else connected; break
            if now > deadline: goto fail
  else: goto fail

Busy-wait de-spin (ClientReceive 949 and HostReceive no-message path):
  if no message this iteration AND not done:
      DoSDLMaintenance()   /* SDL_PollEvent pump - unfreezes window, lets ReadKeyboard work */
      SDL_Delay(1)         /* yield CPU; ~1ms, well under DATA_TIMEOUT granularity */

ChaoticFloat conversion pattern (all PENDING RNG sites):
  VisualRandomFloat2()*S  (range [-S..S])  =>  (ChaoticFloat(stableSeed, modifier)-0.5f)*2.0f*S
  VisualRandomFloat()*S   (range [0..S])   =>  ChaoticFloat(stableSeed, modifier)*S
  VisualRandomLong()&1 boolean             =>  (ChaoticFloat(stableSeed, modifier) < 0.5f)
  VisualRandomLong()&N index               =>  ((uint32_t)(ChaoticFloat(stableSeed, modifier)*(N+1)) & N)
  /* Use a per-event stable seed (node Speed3D/Rot.y/Coord) and a UNIQUE modifier per draw within the
     same event. ChaoticFloat internally mixes gSimulationFrame (Misc.c:246), so same-frame events agree
     across machines; events that fire one frame apart (rubber-band position skew) differ slightly but
     NEVER touch the synced stream, so the fatal seed check (NetHigh.c:876) is unaffected. This is exactly
     why ChaoticFloat - not synced RandomFloat - is the correct Stage-0 tool for position/collision-gated draws. */

Telemetry tick + CSV:
  each frame: record frame time into frameTimeUs[]; update queueDepth[]; track inter-arrival gap (max, >150ms count)
  every ~1s: compute p50/p95/p99 from histogram; append CSV row
      {t, per-client queueDepth/subs/coalesce, maxDeliveryGapMs, rtoShapedGaps, sendRingHW(0), ackDelay(0), p50,p95,p99, seedCheckPasses}
  CSV path from env CMR_NET_CSV; skip if unset.

netns/netem harness (test/netem/cmr_harness.sh outline):
  create bridge cmr_br; for ns in cmr_h cmr_c1 cmr_c2: netns add, veth pair (one end in ns, one on bridge), 10.7.0.x/24, up
  PROFILE() => tc qdisc add dev <veth> root netem <delay/jitter/loss/rate per A..E>
  ip netns exec cmr_h  ./CroMagRally --host --port 49959 --track 1 &
  ip netns exec cmr_c1 ./CroMagRally --join 10.7.0.1 --port 49959 &
  ip netns exec cmr_c2 ./CroMagRally --join 10.7.0.1 --port 49959 &
  run N bot races; collect each instance's CMR_NET_CSV; assert acceptance thresholds; teardown all ns.


**Edge cases:**
- ChaoticFloat keys on gSimulationFrame which 'will diverge in variable-timestep mode' (NetHigh.c:873-875). That is fine and intentional: divergence only perturbs POSITION (absorbed by the 0.2 rubber-band), never the synced-stream draw COUNT, so it can never trip the fatal seed check. Do NOT convert these sites to synced RandomFloat - that is the seed-check coupling commit 89f13c9 deferred them to avoid.
- Stump (Items.c:838) and goddess bolt endpoints (972-974) may be purely visual. Verify collidability/damage-relevance per site before converting; if visual-only, LEAVE on VisualRandom. Mis-converting a visual draw is harmless; mis-leaving a sim draw is a latent desync - err toward converting anything feeding collision/position/damage.
- Sync-mask fix must SKIP bots (isComputer): a player converted to bot by PlayerUnexpectedlyLeavesGame keeps its playerNum but has no live NSp ID and is absent from NSpGame_GetActivePlayersIDMask; including it re-introduces a never-satisfied mask.
- ResetNetGameTransientState must run at BOTH init and teardown (risk-11): init-only misses a dirty global from a crashed prior process; teardown-only misses the very first game. Idempotent memset makes double-calls safe.
- Promoting ClientSend history to file scope changes its lifetime: ensure no path reads it before the first ClientSend after a reset (zero-init is the correct neutral history).
- Anti-spoof check needs gPlayerInfo[p].net.nspPlayerID populated before in-game receive; it is set in HostSendGameConfigInfo (NetHigh.c:594/660) before the game loop. During the char-type phase use the bounds check only (mapping not yet final).
- Non-blocking connect: localhost/netns connect often completes synchronously (rc==0) - handle the fast path; also handle EISCONN on a late poll wake; 3s timeout must not be too short for a loaded CI bridge.
- Busy-wait SDL_Delay(1): the 1ms is within the existing FPS-cap spin budget and does not change lockstep cadence; add it ONLY on the empty-poll branch, never on the message-received path, so throughput is unaffected.
- CLI --join must not swallow a following flag as the address (check argv[i+1] doesn't start with '--'); empty netJoinAddress must cleanly fall back to UDP discovery so interactive play is unchanged.
- Second-game-in-process is the canonical regression test for the reset work: host->end->host must start clean (no wedged player, no carried-over gTimeoutCounter strikes).


**Testing.** DETERMINISM: (1) Profile A 20-race bot soak across the 3 netns, assert zero kNetSequence_SeedDesync and zero spurious fatals (validates RNG conversions did not change synced draw count and the bounds/reset work). (2) Second-hosted-game-in-process: scripted host->end->host, assert clean start (no wedge, gTimeoutCounter==0) - exercises ResetNetGameTransientState. (3) Sparse-lobby: join 3, drop the middle one before start (NSp IDs {0,2}), start race, assert no 8s-timeout wedge (sync-mask fix). (4) Bounds fuzz: inject a kNetClientControlInfoMessage with playerNum=99 and a spoofed from-ID over the harness wire, assert host drops it (no OOB write, no crash). RNG-SPECIFIC: with a fixed seed, log each converted site's host-vs-client output for a scripted race; assert identical when the triggering frame matches AND assert the synced-stream draw cadence (seedCheckPasses) is byte-identical to the pre-change build. HARNESS/HUD/CSV self-test: confirm --host/--join/--port actually launch and connect 3 instances; confirm the HUD shows live queue depth and seedCheckPasses; confirm the CSV row schema matches the METRICS list. UX: confirm the two busy-wait loops no longer pin a core at 100% and the window stays responsive during an induced 1s gap (profile C burst); confirm --join no longer freezes for the OS connect timeout when the host is absent. Netem profiles: A primarily (baseline determinism); B/C lightly to prove the HUD/CSV capture jitter and gaps (full B-E acceptance is Stages 2-5).


**Risks:**
- Mis-classifying a visual draw as sim (or vice-versa): a converted visual draw is harmless but a missed sim draw stays a latent cross-machine desync. Mitigation: per-site collidability/damage audit; default to converting anything feeding collision/position/damage.
- ChaoticFloat changes the game-feel RNG distribution slightly vs old VisualRandom (different generator, frame-keyed) - cosmetic, acceptable for fidelity and matches the already-shipped cactus precedent.
- Sync-mask refactor touches the host's per-frame barrier - a slip wedges every race. Mitigation: keep queue/counter indexing playerNum-based, change ONLY the mask identity; cover with the sparse-lobby test before merge.
- Anti-spoof from-ID check could wrongly reject legit messages if nspPlayerID mapping is ever stale; gate it so a mismatch drops only the single message (not the connection) and logs it - degrades gracefully.
- Restoring --host/--join re-enables the headless auto-start path (Main.c:1823) dead since a5f48a9; verify it still drives SetupNetworkHosting/Join correctly on current master (surrounding code may have drifted).
- netns/netem needs root/CAP_NET_ADMIN and models delay/loss not 802.11 MAC retry/power-save; necessary but not sufficient - real-device soak still required in Stage 5. Document in the harness README.
- CSV/HUD plumbing is added now but mostly fed later; keep unfilled fields explicitly zeroed so a reader never mistakes 'not yet instrumented' for 'measured zero'.
- -ffp-contract=off may shift positions slightly on ARM vs the shipped build (different rounding); harmless under rubber-band but a one-time delta - validate with an x86<->ARM profile-A soak before claiming determinism parity.


## Stage 1 — Non-blocking sends (per-socket send rings)

**Effort:** 2-3 dev-days. Self-contained in NetLow.c (rings + SendOrEnqueue + NSpGame_FlushSends), a one-line Net_Pump wrapper in NetHigh.c, and three call-site insertions in Main.c/NetHigh.c. No wire-format change, no 'CMR6' bump, no receive-path or game-logic change -> independently shippable as a patch release on top of Stage 0. Soft dependency: Stage 0(g)'s SDL_Delay(1) in the two busy loops makes the in-loop flush well-behaved.

**Objective.** Remove the verified 200-600ms/frame main-thread stall caused by SendOnSocket's blocking EWOULDBLOCK retry loop (NetLow.c:1700-1746, up to 100×SDL_Delay(2) per socket, ×3 clients on a host broadcast). Replace it with: (a) one non-blocking send() attempt per message, (b) a per-socket 32 KB byte-ring that absorbs whatever the kernel send buffer can't take, (c) NSpGame_FlushSends() draining those rings every Net_Pump, and (d) ring overflow (~2s backlog) routed to the existing kick (host) / game-terminate (client) paths. No wire-format change, no protocol bump, no receive-path change. After this stage a slow/stalled client's full TCP send buffer can never freeze the host frame; it only fills that client's ring and, if it never drains, gets that one client kicked while everyone else runs uninterrupted. Independently shippable as a patch on top of Stage 0.


**Changes:**

- `Source/Network/NetLow.c` (top of file, after the constants block (~NetLow.c:55, near SOCKET_SNDBUF_SIZE)) — Add #define SEND_RING_CAPACITY 32768 and the SendRing struct + helper prototypes (SendRing_Reset/FreeSpace/Append/Drain).
- `Source/Network/NetLow.c` (struct NSpPlayer (NetLow.c:65-71)) — Add field `SendRing sendRing;` — the host's outbound byte queue for this peer's socket. It is auto-reset by NSpPlayer_Clear's existing memset, so no extra teardown wiring is needed for peer rings.
- `Source/Network/NetLow.c` (struct NSpGame (NetLow.c:73-90)) — Add field `SendRing clientSendRing;` — the client's outbound byte queue for clientToHostSocket (the client doesn't use players[].sockfd for its uplink).
- `Source/Network/NetLow.c` (new SendRing helpers, place just above SendOnSocket (~NetLow.c:1677)) — Implement SendRing_Reset (head=tail=used=0), SendRing_FreeSpace (CAPACITY-used), SendRing_Append (copy with wrap-around, return false on overflow), SendRing_Drain (non-blocking send() of contiguous runs until empty or EWOULDBLOCK; return kNSpRC_SendFailed on peer-close/hard error).
- `Source/Network/NetLow.c` (SendOnSocket (NetLow.c:1679-1750)) — RENAME the existing function verbatim to SendBytesBlocking (keep its body, INCLUDING the 100×SDL_Delay(2) loop) — it is retained ONLY for the two non-gameplay one-shot sends (lobby join-request and the game-full denial). Then ADD a new static SendOrEnqueue(sockfd_t sockfd, SendRing* ring, NSpMessageHeader* header): one non-blocking send() attempt when the ring is empty, append the remainder/whole message to the ring otherwise, return kNSpRC_SendFailed on overflow or hard socket error.
- `Source/Network/NetLow.c` (NSpMessage_Send broadcast branch (NetLow.c:1788)) — Replace SendOnSocket(game->players[i].sockfd, header) with SendOrEnqueue(game->players[i].sockfd, &game->players[i].sendRing, header). On failure keep the existing kickOnFail → NSpPlayer_Kick path (overflow→kick).
- `Source/Network/NetLow.c` (NSpMessage_Send unicast branch (NetLow.c:1813)) — Replace SendOnSocket(peer->sockfd, header) with SendOrEnqueue(peer->sockfd, &peer->sendRing, header) (peer is already resolved via NSpGame_GetPlayerFromID). Keep kick-on-fail.
- `Source/Network/NetLow.c` (NSpMessage_Send client branch (NetLow.c:1832-1840)) — Replace SendOnSocket(game->clientToHostSocket, header) with SendOrEnqueue(game->clientToHostSocket, &game->clientSendRing, header). On failure (overflow/hard error) CloseSocket(&game->clientToHostSocket) + SendRing_Reset(&game->clientSendRing) so the next NSpMessage_GetAsClient/PollSocket synthesizes kNSpGameTerminated(NetworkError) — i.e. client overflow is fatal via the existing teardown path (no NetLow→NetHigh fatal-call dependency).
- `Source/Network/NetLow.c` (JoinLobby (NetLow.c:424) and NSpGame_AcceptNewClient denial (NetLow.c:500)) — Point these two one-shot sends at the renamed SendBytesBlocking (no ring exists there: JoinLobby's socket isn't in the game struct until line 435; the denial socket is closed immediately after). These are lobby/teardown, not per-frame, so a brief block is acceptable.
- `Source/Network/NetLow.c` (NSpGame_AcceptNewClient slot-found branch (NetLow.c:482-491)) — Add SendRing_Reset(&newPlayer->sendRing) when occupying a slot, so a slot reused after a previous kick starts with an empty ring (defensive; NSpPlayer_Clear already zeroed it on the prior kick).
- `Source/Network/NetLow.c` (new public function NSpGame_FlushSends, place near NSpMessage_Send (~NetLow.c:1751)) — Implement NSpGame_FlushSends(NSpGameReference): host → for each valid peer slot, SendRing_Drain(&players[i].sendRing, sockfd); on failure NSpPlayer_Kick(gameRef, players[i].id). Client → SendRing_Drain(&clientSendRing, clientToHostSocket); on failure CloseSocket+SendRing_Reset (same teardown as the client branch above). No-op on NULL game.
- `Source/Headers/netsprocket.h` (prototype block (~netsprocket.h:173, near NSpGame_Dispose)) — Declare `void NSpGame_FlushSends(NSpGameReference gameRef);`.
- `Source/Network/NetHigh.c` (new function Net_Pump, place near top of file after globals (~NetHigh.c:62)) — Implement `void Net_Pump(void) { if (!gNetGameInProgress || gNetGame == nil) return; NSpGame_FlushSends(gNetGame); }`. (Stages 2-3 will extend this to also drain receive sockets; for Stage 1 it is flush-only.)
- `Source/Headers/network.h` (prototype block (network.h:109-113)) — Declare `void Net_Pump(void);`.
- `Source/System/Main.c` (PlayArea game loop, top of while(true) after startTick (Main.c:1024-1025)) — Insert Net_Pump(); so any backlog drains at frame start (cheap no-op when rings are empty).
- `Source/Network/NetHigh.c` (ClientReceive_ControlInfoFromHost busy loop, no-message else branch (NetHigh.c:949-973)) — Call Net_Pump() once per loop iteration so the client uplink ring keeps draining while blocked waiting for the host packet (prevents a deadlock: host blocked in HostReceive waiting for client input that is stuck in the client's send ring). Place it adjacent to the SDL_Delay(1) that Stage 0(g) adds so it runs ~1 kHz, not in a zero-sleep hot spin.
- `Source/Network/NetHigh.c` (HostReceive_ControlInfoFromClients busy loop, top of each iteration (NetHigh.c:1180)) — Call Net_Pump() once per loop iteration so the host's broadcast (enqueued in Step 1) keeps draining toward a slow client while the host waits for inputs. Same throttling note as the client loop.


**Data structures.** // --- NetLow.c, near the constants block (~line 55) ---
#define SEND_RING_CAPACITY 32768   // 32 KB per socket.
// Host msg ~246-290B wire @60pps => ~113 msgs ~1.9s before overflow ("~2s kick").
// Client msg ~150B (pre-Stage-2) => ~218 msgs ~3.6s. Single max msg = 284B << 32768,
// so a message always fits an empty ring; overflow only comes from accumulation.

typedef struct SendRing
{
    uint32_t  head;                    // read cursor: next byte to send()
    uint32_t  tail;                    // write cursor: next byte to append
    uint32_t  used;                    // bytes currently queued (0..SEND_RING_CAPACITY)
    uint32_t  highWater;               // telemetry: max 'used' ever (Stage 0 HUD/CSV)
    uint8_t   data[SEND_RING_CAPACITY];
} SendRing;

// --- struct NSpPlayer (NetLow.c:65-71) gains: ---
//     SendRing  sendRing;            // host-side outbound queue for this peer socket
//                                    // (auto-reset by NSpPlayer_Clear's memset)

// --- struct NSpGame (NetLow.c:73-90) gains: ---
//     SendRing  clientSendRing;      // client-side outbound queue for clientToHostSocket
//                                    // (zero-init by AllocPtrClear in NSpGame_Alloc)

// Note: sizeof(NSpGame) grows by MAX_CLIENTS(4)*32KB (peer rings) + 32KB (client ring)
// = ~160 KB, heap-allocated once per session via AllocPtrClear and freed in
// NSpGame_Dispose -> rings can never carry state across consecutive games
// (unlike the static sHostInputQueues bug).


**Algorithms.** RING PRIMITIVES (NetLow.c):

SendRing_FreeSpace(r): return SEND_RING_CAPACITY - r->used

SendRing_Append(r, src, len):           // returns false on overflow
    if (len > SendRing_FreeSpace(r)) return false
    first = min(len, SEND_RING_CAPACITY - r->tail)   // contiguous run to buffer end
    memcpy(r->data + r->tail, src, first)
    if (len > first) memcpy(r->data, src + first, len - first)   // wrap
    r->tail = (r->tail + len) % SEND_RING_CAPACITY
    r->used += len
    if (r->used > r->highWater) r->highWater = r->used
    return true

SendRing_Drain(r, sockfd):              // non-blocking; returns kNSpRC_OK / kNSpRC_SendFailed
    while (r->used > 0):
        run = min(r->used, SEND_RING_CAPACITY - r->head)   // contiguous bytes from head
        sent = send(sockfd, r->data + r->head, run, MSG_NOSIGNAL)
        if (sent > 0):
            r->head = (r->head + sent) % SEND_RING_CAPACITY
            r->used -= sent
            continue
        if (sent == 0): return kNSpRC_SendFailed           // peer closed
        err = GetSocketError()
        if (err == kSocketError_WouldBlock): return kNSpRC_OK   // buffer full; retry next pump
        return kNSpRC_SendFailed                            // EPIPE/ECONNRESET/etc -> dead
    return kNSpRC_OK


SendOrEnqueue(sockfd, ring, header):    // replaces per-frame use of SendOnSocket
    assert(header->what != kNSpUndefinedMessage)
    assert(header->messageLen between sizeof(NSpMessageHeader) and kNSpMaxMessageLength)
    assert(header->version == kNSpCMRProtocol4CC)
    if (!IsSocketValid(sockfd)) return kNSpRC_InvalidSocket
    msgLen = header->messageLen

    // CRITICAL ordering rule: if ANY bytes are already queued, we must NOT send fresh
    // bytes directly (that would interleave out of order in the TCP stream). Append only.
    if (ring->used == 0):
        sent = send(sockfd, (char*)header, msgLen, MSG_NOSIGNAL)
        if (sent == msgLen) return kNSpRC_OK                // healthy path: out immediately
        if (sent > 0):
            return SendRing_Append(ring, (uint8*)header + sent, msgLen - sent)
                   ? kNSpRC_OK : kNSpRC_SendFailed          // queue the remainder
        if (sent == 0) return kNSpRC_SendFailed             // peer closed
        if (GetSocketError() != kSocketError_WouldBlock) return kNSpRC_SendFailed
        // EWOULDBLOCK: fall through and queue the whole message
    return SendRing_Append(ring, (uint8*)header, msgLen) ? kNSpRC_OK : kNSpRC_SendFailed
    // false here == OVERFLOW (~2s backlog) -> caller's existing kick / terminate path


NSpGame_FlushSends(gameRef):            // called every Net_Pump
    game = Unbox(gameRef); if (!game) return
    if (game->isHosting):
        for i in 0..MAX_CLIENTS-1:
            peer = &game->players[i]
            if (!IsSocketValid(peer->sockfd)): SendRing_Reset(&peer->sendRing); continue
            if (SendRing_Drain(&peer->sendRing, peer->sockfd) != kNSpRC_OK):
                NSpPlayer_Kick(gameRef, peer->id)   // closes socket; NSpPlayer_Clear resets ring
            // index-based loop + in-place clear => safe to kick mid-iteration;
            // kick's PlayerLeft broadcast appends to OTHER slots' rings (fine).
    else:
        if (IsSocketValid(game->clientToHostSocket)):
            if (SendRing_Drain(&game->clientSendRing, game->clientToHostSocket) != kNSpRC_OK):
                CloseSocket(&game->clientToHostSocket)      // next Get -> GameTerminated
                SendRing_Reset(&game->clientSendRing)


INTEGRATION:
  Net_Pump():  if (gNetGameInProgress && gNetGame) NSpGame_FlushSends(gNetGame)
  - Main.c PlayArea: call Net_Pump() at top of the frame loop (Main.c:1025).
  - NetHigh.c ClientReceive busy loop: call Net_Pump() per iteration (drains uplink while
    blocked) -- REQUIRED to avoid host<->client mutual-wait deadlock.
  - NetHigh.c HostReceive busy loop: call Net_Pump() per iteration (drains the broadcast to a
    slow client while host waits for inputs) -- REQUIRED for the same reason.

HEALTHY-PATH NOTE: SO_SNDBUF=64KB (ApplyTCPSocketOptions) means a ~290B message's single
send() in SendOrEnqueue essentially always completes fully, so on clean/wired links the ring
stays empty and end-to-end latency is identical to today (minus the deleted retry stalls).
The ring is pure backpressure storage for the slow-client case.


**Edge cases:**
- TCP byte-stream ordering: never send fresh bytes while ring->used>0 (would interleave). SendOrEnqueue's 'append-whole-when-non-empty' rule guarantees in-order delivery.
- Partial send: send() returns 0<sent<msgLen on an empty ring -> exactly (msgLen-sent) remainder bytes appended; next drain continues from there.
- Ring wrap-around: Append and Drain both split copies/sends at the SEND_RING_CAPACITY boundary (two memcpy / per-run send).
- Overflow on host (~2s of un-ACKed broadcasts): SendRing_Append returns false -> SendOrEnqueue returns kNSpRC_SendFailed -> existing kickOnFail kicks only that client; host and other clients unaffected.
- Overflow on client: failure closes clientToHostSocket so the existing PollSocket brokenPipe path synthesizes kNSpGameTerminated(NetworkError) -> clean fatal, no NetLow->NetHigh coupling.
- Hard socket error (EPIPE/ECONNRESET) mid-drain: distinguished from EWOULDBLOCK via GetSocketError(); treated as dead -> kick/terminate. MSG_NOSIGNAL prevents SIGPIPE.
- Kick reentrancy: NSpPlayer_Kick (called from FlushSends or the broadcast loop) broadcasts PlayerLeft via NSpMessage_Send, which appends into OTHER slots' rings. Index-based iteration + in-place NSpPlayer_Clear keep pointers valid; the kicked slot's ring is auto-reset by the memset.
- Kick bye-message delivery: NSpPlayer_Kick sends the YouGotKicked bye via NSpMessage_Send then immediately closes the socket. The tiny (~32B) message goes out through SendOrEnqueue's single send() into the kernel buffer (which close() still flushes); only a ring remainder would be lost, and that only when the buffer is already full (the dead-connection case). Acceptable.
- Lobby/handshake sends (JoinLobby join-request, AcceptNewClient denial): kept on the blocking SendBytesBlocking helper because no per-socket ring exists at those points and they are one-shot, non-gameplay.
- Empty/non-net frames: Net_Pump guards on gNetGameInProgress && gNetGame==nil, so the top-of-loop call is a no-op outside net games.
- Second net game in the same process: gNetGame is freed (NSpGame_Dispose) and re-AllocPtrClear'd per session, so all rings start empty -> no stale-byte carryover (contrast the static sHostInputQueues Stage-0 bug).
- Slot reuse after kick: AcceptNewClient resets the new slot's sendRing defensively (NSpPlayer_Clear on the prior kick already cleared it).
- Busy-loop flush frequency: the in-loop Net_Pump must sit next to Stage 0(g)'s SDL_Delay(1) so it runs ~1 kHz; without that sleep a zero-sleep spin would issue send() syscalls at ~100 kHz (wasteful, not incorrect).


**Testing.** Build the Stage-0 netns+netem harness (ip netns cmr_h/cmr_c1/cmr_c2 over veth/bridge, game per-ns via --host/--join/--port). STAGE-1-SPECIFIC induced backpressure (the class this stage fixes): apply `tc qdisc add dev <veth-toward-c1> root netem rate 1mbit limit 5` so the host's TCP send buffer to c1 saturates. EXPECTED: with old code the host frame-time spikes to 200-600ms (SendOnSocket retry); with the new code host frame-time p99 stays ~1F (16.7ms) while c1's send-ring highWater climbs in the CSV/HUD without affecting frame time; c2 (clean) shows zero disturbance; if the throttle persists past ~2s, c1's ring overflows and c1 alone is kicked while host+c2 keep running. HARSH STALL TEST: SIGSTOP a client process for ~1.5s mid-race -> host must not freeze (enqueues), then either client resumes (ring drains, race continues) or overflow kicks just that client. REGRESSION: profile A (0ms, clean) 20-race all-bot soak -> zero kNetSequence_SeedDesync, zero kNetSequence_ErrorSendFailed, frame-time histogram p50/p95/p99 unchanged vs pre-Stage-1 baseline (proves no added latency on healthy links), and a second hosted game in the same process starts clean (fresh rings). METRICS to add/log: per-socket sendRing.highWater bytes (peak), kick-on-overflow count, host frame-time p99 under the rate-limit profile.


**Risks:**
- DEADLOCK if rings aren't flushed inside the still-blocking Stage-1 receive loops: host waits in HostReceive for client input stuck in the client's send ring, while the client waits in ClientReceive for the host broadcast stuck in the host's send ring. MITIGATION (mandatory): the per-iteration Net_Pump() calls added to both busy loops. This is the #1 correctness risk of the stage.
- TCP reordering if SendOrEnqueue ever emits fresh bytes while the ring is non-empty. MITIGATION: strict append-whole-when-non-empty rule; covered by a localhost ordered-delivery assertion in the soak (frame counters must stay monotonic).
- Kick-on-overflow may evict a momentarily-slow-but-recoverable WiFi client at the ~2s threshold. This stage intentionally only removes the host stall; the real WiFi smoothing (adaptive depth + substitution) is Stage 2. MITIGATION: 32KB (~2s) sizing + log a warning before kick; don't tune the threshold here.
- Kick reentrancy / iteration-safety in FlushSends and the broadcast loop. MITIGATION: index-based iteration + in-place NSpPlayer_Clear; covered by the SIGSTOP-then-overflow test.
- Possible loss of a kick bye-message remainder if the socket buffer is already full at close. Low impact (only on already-dead connections); the single send() into the kernel buffer covers the normal case.
- sizeof(NSpGame) grows ~160KB and NSpPlayer_Clear now memsets 32KB per kick/accept (cold paths) — negligible on desktop; note for the smallest mobile targets.
- Busy-loop flush can spin send() at high frequency if Stage 0(g)'s SDL_Delay(1) is not present in the loop yet (soft dependency). MITIGATION: ship Stage 1 after/with Stage 0(g), or co-locate the flush with a minimal sleep.


## Stage 2 — CMR7 bump + host-never-waits + adaptive depth + wall-clock sender + host-side edge re-derivation

**Effort:** 5-7 dev-days, independently shippable. The host-side stutter fix (host never blocks on any client's radio) is complete and demonstrable on its own after this stage; client free-running/catch-up/hold-last is deferred to Stage 3. Breakdown: wire structs + 4CC/cap bump + _Static_asserts ~0.5d; Host_PumpClientInputs + Host_ConsumeClientInputs (substitution/coalesce/edge derivation/bookkeeping) ~2d; adaptive depth controller + per-client seeding/reset ~1d; wall-clock sampler + pre-roll replacement + Main.c reordering ~1d; HostSend/ClientSend field rework + ResetNetGameTransientState ~0.5d; test/HUD/CSV validation against profiles A/C/E + cross-arch seed soak ~1-2d. Assumes Stage 0 (RNG hygiene, busy-loop sleep, bounds validation) and Stage 1 (send rings + Net_Pump) are already merged.

**Objective.** Eliminate the primary WiFi stutter mechanism on the host side: replace the blocking, 4-strike HostReceive busy-spin (NetHigh.c:1166-1263, called Main.c:1122-1125) with a non-blocking pump + per-client input substitution/coalescing so the host NEVER waits on any client's radio. Bump the wire protocol once ('CMR6'->'CMR7', payload cap 256->512) and land ALL CMR7 fields now so Stages 3-6 change zero wire bytes. Move edge derivation (controlBitsNew) entirely host-side using the engine's own formula, deleting the dead 8-frame redundancy history + RecoverFromFuture. Introduce per-client adaptive input-queue depth D_i seeded from each joiner's own WiFi hint (retiring the global gUseRedundancy all-or-nothing switch and the 30-frame ~500ms pre-roll), and a wall-clock-paced client input sampler so the client's uplink cadence is decoupled from packet consumption. Determinism is preserved by construction (clients still only simulate the host echo: Main.c local write at Step 3 is overwritten by the host packet at NetHigh.c:882-887 before any sim step; the per-frame MyRandomLong seed check at NetHigh.c:876-880 is kept untouched). Host-side stutter fix is COMPLETE and independently shippable; the client free-running consume loop is Stage 3.


**Changes:**

- `Source/Headers/netsprocket.h` (netsprocket.h:21) — kNSpMaxPayloadLength 256 -> 512. kNSpMaxMessageLength derives automatically. Must be in the SAME commit as the 4CC bump.
- `Source/Headers/netsprocket.h` (netsprocket.h:24) — kNSpCMRProtocol4CC 'CMR6' -> 'CMR7'. Old peers are cleanly rejected by the version check at NetLow.c:718-723 and the send-side assert at NetLow.c:1685.
- `Source/Headers/network.h` (network.h:41-44 NetSyncMessage) — Add uint16_t targetFPS + uint16_t pad. Field lands in the CMR7 bump but is only WRITTEN/READ in Stage 4 (host-finalized FPS negotiation). Add _Static_assert on sizeof.
- `Source/Headers/network.h` (network.h:49-63 NetHostControlInfoMessageType) — Rewrite per CMR7 (see dataStructures): keep fps/fpsFrac/randomSeed/frameCounter/simTick/controlBits[]/controlBitsNew[] (now host-derived)/analogSteering[]/pauseState[]/syncPos[]/syncRotY[]; ADD inputFlags[MAX_PLAYERS] (bit0 substituted, bit1 coalesced), ackInputSeq[MAX_PLAYERS], queueDepth[MAX_PLAYERS], targetDepth[MAX_PLAYERS], eventCount + NetFrameEvent events[2]. Add NetFrameEvent struct + kEv*/INPUT_FLAG_* enums. Group same-alignment fields; add exact-size _Static_assert.
- `Source/Headers/network.h` (network.h:68-82 NetClientControlInfoMessageType) — Rewrite (see dataStructures): rename frameCounter -> inputSeq (client-owned monotonic); ADD uint32_t lastHostFrameSeen + explicit pad; DELETE controlBitsNew, prevControlBits[8], prevAnalogSteering[8] (host derives edges; TCP is ordered so history was dead weight). Add _Static_assert.
- `Source/Headers/network.h` (network.h:35 NetConfigMessage.useRedundancy) — Rename field to 'reserved' (keep the byte for layout); stop writing/reading it. gUseRedundancy global retired.
- `Source/Headers/network.h` (network.h:109-113 prototypes) — Remove HostReceive_ControlInfoFromClients decl; add Host_PumpClientInputs(void), Host_ConsumeClientInputs(void), Host_InitInputControl(void), SampleAndSendLocalInput(Boolean* outSchedulePause), ResetNetGameTransientState(void). (Net_Pump/NSpGame_FlushSends come from Stage 1.) Leave Client_CheckIfMorePacketsWaiting decl (deleted in Stage 3).
- `Source/Network/NetHigh.c` (HostSend_ControlInfoToClients (794-844)) — Extend message fill: keep frameCounter/fps/randomSeed/simTick and the per-player controlBits/controlBitsNew/analogSteering/pauseState/syncPos/syncRotY loop (controlBitsNew[i] is now whatever Host_ConsumeClientInputs/GetLocalKeyState/AI put in gPlayerInfo[i].controlBits_New). ADD: ackInputSeq[i]=sLastApplied[i].lastAppliedSeq, queueDepth[i]=Queue_Count(i), targetDepth[i]=sDepth[i].targetDepth, inputFlags[i]=sInputFlags[i], eventCount=0 (events populated in Stage 4).
- `Source/Network/NetHigh.c` (Client_InGame_HandleHostControlInfoMessage (853-911)) — Keep verbatim: dup-drop (859), lost-packet fatal (862-866), fps adoption (870-871), MyRandomLong seed check (876-880), input copy incl mess->controlBitsNew (882-887), 0.2 rubber-band (889-907). Optionally cache mess->ackInputSeq[gMyNetworkPlayerNum] into a global for the debug HUD (own-input delay = (ownSeq-ack)*F). No logic change.
- `Source/Network/NetHigh.c` (ClientSend_ControlInfoToHost (1033-1091)) — Delete redundancy history block (1036-1037 statics, shift 1043-1059, copy 1077-1084) and the controlBits_New write (1073). Write inputSeq=gClientSendCounter[gMyNetworkPlayerNum]++, playerNum, controlBits, analogSteering, pauseState, lastHostFrameSeen=gHostSendCounter. Keep SendOrEnqueue (Stage 1) as the send.
- `Source/Network/NetHigh.c` (NET_QUEUE_SIZE (1096) + Queue_* (1107-1133)) — NET_QUEUE_SIZE 64 -> 128 (~2s @60). Add static int Queue_Count(int playerNum). Keep Queue_Push/Peek/Pop; Queue_Push additionally feeds the depth jitter ring (timestamp delta).
- `Source/Network/NetHigh.c` (ApplyClientMessage (1136-1143)) — DELETE. Its copy-into-gPlayerInfo logic is folded into Host_ConsumeClientInputs with host-side edge derivation (it can no longer copy controlBitsNew because the wire field is gone).
- `Source/Network/NetHigh.c` (RecoverFromFuture (1146-1164)) — DELETE entirely (depends on deleted prev* history; TCP ordering makes true gaps impossible).
- `Source/Network/NetHigh.c` (HostReceive_ControlInfoFromClients (1166-1263)) — REPLACE with two functions: Host_PumpClientInputs (non-blocking drain of all readable kNetClientControlInfoMessage into sHostInputQueues with arrival timestamping; others -> HandleOtherNetMessage) and Host_ConsumeClientInputs (depth controller + substitution + coalesce + single-apply with edge derivation). Delete the whole AreAllPlayersSynced/MarkPlayerSynced spin, the TickCount DATA_TIMEOUT block, and the 4-strike gTimeoutCounter logic (1252-1261).
- `Source/Network/NetHigh.c` (new statics near 1096) — Add sLastApplied[MAX_PLAYERS] (HostClientInputState), sDepth[MAX_PLAYERS] (HostClientDepthState), sInputFlags[MAX_PLAYERS], sClientConnectionHint[MAX_PLAYERS]; helpers Host_UpdateDepth(i), Host_DeriveEdges(prevBits,newBits), Host_SeedDepth(i).
- `Source/Network/NetHigh.c` (char-type handler (405-409)) — Replace 'if (mess->connectionType==1) gUseRedundancy=true;' with 'sClientConnectionHint[mess->playerNum] = mess->connectionType;' (per-client seed for D_init). Keep the playerNum bounds check already present at 387-391.
- `Source/Network/NetHigh.c` (SetupNetworkHosting (487-492) + HandleGameConfigMessage (674-677)) — Stop seeding/propagating gUseRedundancy (lines 491, 629, 675). Net_GetConnectionHint() is KEPT, demoted to per-client D_init seed (host's own at sClientConnectionHint[gMyNetworkPlayerNum], clients via NetPlayerCharTypeMessage.connectionType which is still broadcast at 1412).
- `Source/Network/NetHigh.c` (EndNetworkGame (188-224)) — Factor the counter resets into new ResetNetGameTransientState() and call it here AND from the host init path. It must zero: sHostInputQueues, sLastApplied, sDepth, sInputFlags, sClientConnectionHint, gTimeoutCounter, gHostSendCounter, gClientSendCounter, and the sampler's nextSampleTime (fixes the second-game-in-process wedge; risk 11).
- `Source/Network/NetHigh.c` (new Host_InitInputControl()) — Called once by the host just before the game loop: ResetNetGameTransientState() already zeroed state; seed sDepth[i].targetDepth = (sClientConnectionHint[i]==1)?6:2 for each active remote client (D_init). Bots/host slot left at default.
- `Source/Network/NetHigh.c (or NetLow.c if Stage 1 owns it)` (Net_Pump()) — Stage 1 provides Net_Pump (flush send rings + drain). Stage 2 extends the HOST branch of Net_Pump to call Host_PumpClientInputs() for the drain-to-queues. Client branch = NSpGame_FlushSends() only (host packets still consumed by ClientReceive in Stage 2; Stage 3 swaps in Client_PumpHostPackets).
- `Source/Network/NetHigh.c` (new SampleAndSendLocalInput(Boolean* outSchedulePause)) — Wall-clock client input sampler (G1). while (now>=sNextSampleTime && burst<3){ ReadKeyboard(); GetLocalKeyState(); *outSchedulePause |= GetNewNeedStateAnyP(kNeed_UIPause); gPlayerInfo[me].net.pauseState = *outSchedulePause; ClientSend_ControlInfoToHost(); sNextSampleTime += 1/gTargetFPS; burst++; } Resync sNextSampleTime to now after gaps >250ms (app suspend). Keep a build flag CMR7_SEND_PER_FRAME to revert to one-send-per-frame for A/B (risk 3).
- `Source/System/Main.c` (PlayArea pre-roll (1004-1018)) — Replace the gUseRedundancy 30-frame burst with D_init duplicate sends: int dinit = (Net_GetConnectionHint()==1)?6:2; for(i=0;i<dinit;i++) ClientSend_ControlInfoToHost(); (client only). Each duplicate increments gClientSendCounter[me] so the host banks D_init real packets.
- `Source/System/Main.c` (PlayArea host branch of Step 1 (1042-1046)) — Replace 'HostSend_ControlInfoToClients()' with: Net_Pump(); Host_ConsumeClientInputs(); HostSend_ControlInfoToClients();  (drain -> resolve per-client inputs for THIS frame -> broadcast, all before simulation).
- `Source/System/Main.c` (PlayArea Step 3 (1075-1084)) — Split: if client -> SampleAndSendLocalInput(&schedulePause); else (host) keep ReadKeyboard()+GetLocalKeyState()+schedulePause=GetNewNeedStateAnyP(kNeed_UIPause)+gPlayerInfo[me].net.pauseState=schedulePause (host reads its own input here for next frame; ~1F latency preserved). Add Net_Pump()/NSpGame_FlushSends() for the client so the sampler's enqueued packets flush.
- `Source/System/Main.c` (PlayArea end-of-frame HostReceive (1122-1125)) — DELETE the HostReceive_ControlInfoFromClients() call. Host input intake now happens at top-of-frame via Net_Pump + Host_ConsumeClientInputs.
- `Source/System/Main.c` (PlayArea before loop (host)) — Call Host_InitInputControl() once (host) after the level-prep barrier, before the while(true) loop, to seed per-client D_init.
- `Source/System/Main.c:27 / Source/Headers/game.h:104` (gUseRedundancy global) — Retire: remove all remaining reads. May leave the definition in place (unused) to minimize churn, or delete with the config-field rename. No machine behavior keys off it after this stage.


**Data structures.** // ---- netsprocket.h ----
#define kNSpMaxPayloadLength 512        // was 256
#define kNSpCMRProtocol4CC 'CMR7'       // was 'CMR6'

// ---- network.h : new enums + frame-event struct (fields land now, used Stage 4) ----
enum { kEvReserved=0, kEvBecomeBot=1, kEvUnpauseForce=2 };
enum { INPUT_FLAG_SUBSTITUTED=0x01, INPUT_FLAG_COALESCED=0x02 };

typedef struct {
    uint32_t  effectiveFrame;   // host sim frame at which every machine applies it
    uint8_t   type;             // kEv*
    int8_t    playerNum;
    uint16_t  pad;
} NetFrameEvent;                // expect sizeof==8
_Static_assert(sizeof(NetFrameEvent)==8, "NetFrameEvent ABI");

// ---- network.h : H->C broadcast (CMR7) ----
typedef struct {
    NSpMessageHeader  h;
    float             fps, fpsFrac;
    uint32_t          randomSeed;                  // kept fatal seed check
    uint32_t          frameCounter;                // host frame (gHostSendCounter)
    uint32_t          simTick;                     // gSimulationFrame (diagnostic)
    uint32_t          controlBits[MAX_PLAYERS];
    uint32_t          controlBitsNew[MAX_PLAYERS]; // HOST-DERIVED edges
    OGLVector2D       analogSteering[MAX_PLAYERS];
    OGLPoint3D        syncPos[MAX_PLAYERS];         // rubber-band feed (kept)
    float             syncRotY[MAX_PLAYERS];
    uint32_t          ackInputSeq[MAX_PLAYERS];     // last REAL input seq applied per player
    uint8_t           pauseState[MAX_PLAYERS];
    uint8_t           inputFlags[MAX_PLAYERS];      // bit0 substituted, bit1 coalesced
    uint8_t           queueDepth[MAX_PLAYERS];      // telemetry
    uint8_t           targetDepth[MAX_PLAYERS];     // telemetry
    uint8_t           eventCount;                   // 0..2 (0 until Stage 4)
    NetFrameEvent     events[2];
} NetHostControlInfoMessageType;   // ~305B incl 28B header, < 540 cap
_Static_assert(sizeof(NetHostControlInfoMessageType) <= kNSpMaxMessageLength, "host msg fits");

// ---- network.h : C->H (CMR7) ----
typedef struct {
    NSpMessageHeader  h;
    int16_t           playerNum;
    uint8_t           pauseState;
    uint8_t           pad;
    uint32_t          inputSeq;            // monotonic, client-owned (was frameCounter)
    uint32_t          lastHostFrameSeen;   // RTT/diagnostics
    uint32_t          controlBits;
    OGLVector2D       analogSteering;
} NetClientControlInfoMessageType;         // 52B wire (28+24)
_Static_assert(sizeof(NetClientControlInfoMessageType) <= kNSpMaxMessageLength, "client msg fits");

// ---- network.h : sync message gains FPS field (used Stage 4) ----
typedef struct { NSpMessageHeader h; uint16_t targetFPS; uint16_t pad; } NetSyncMessage;

// ---- NetHigh.c : host-side per-client state ----
#define NET_QUEUE_SIZE 128                 // was 64 (~2s @60)
#define JITTER_WINDOW  128
#define GRACE_RETRIES  2                    // tunable 0..N; 0 disables grace (each ~1ms)
#define SUB_DECAY_AFTER 30                  // substituted frames before neutral decay

typedef struct {
    uint32_t     controlBits;       // last REAL bits = edge-derivation baseline (NOT decayed)
    OGLVector2D  analogSteering;    // last REAL analog (held while substituting)
    uint8_t      pauseState;        // last REAL pause (HELD on substitute -> never spurious unpause)
    uint32_t     lastAppliedSeq;    // == ackInputSeq broadcast; stale rule: seq <= this -> discard
    Boolean      valid;             // false until first real input applied this race
    uint16_t     subStreak;         // consecutive substituted frames
} HostClientInputState;
static HostClientInputState sLastApplied[MAX_PLAYERS];

typedef struct {
    uint64_t  lastArrivalPC;        // SDL_GetPerformanceCounter of previous packet (0=none)
    float     deltas[JITTER_WINDOW];// inter-arrival deltas (seconds)
    int       count, head;          // ring fill / write idx
    uint8_t   targetDepth;          // D_i, seeded D_init, clamp [1,8]
    float     decayAccumSec;        // accumulator for 1-frame-per-2s decay
    uint32_t  baselineFrameTick;    // throttle P95 recompute to <=4Hz
} HostClientDepthState;
static HostClientDepthState sDepth[MAX_PLAYERS];

static uint8_t sInputFlags[MAX_PLAYERS];        // rebuilt each consume frame
static uint8_t sClientConnectionHint[MAX_PLAYERS]; // 0 wired / 1 wifi, from char-type msg

// ---- Main.c / NetHigh.c : wall-clock sampler ----
static double sNextSampleTime = 0.0;            // seconds, SDL_GetPerformanceCounter base
#define MAX_SAMPLE_BURST 3


**Algorithms.** ### Host_PumpClientInputs() (called from Net_Pump, host branch; non-blocking)
loop:
  m = NSpMessage_Get(gNetGame)          // round-robin, one msg/call (NetLow.c:783-836)
  if m==NULL: break
  if m->what == kNetClientControlInfoMessage:
      c = (NetClientControlInfoMessageType*)m
      if m->messageLen >= sizeof(*c) && IsValidPlayerNum(c->playerNum)
         && !gPlayerInfo[c->playerNum].isComputer && c->playerNum != gMyNetworkPlayerNum:
          uint64_t now = SDL_GetPerformanceCounter()
          if sDepth[c->playerNum].lastArrivalPC != 0:
              float dt = (now - sDepth[c->playerNum].lastArrivalPC)/perfFreq
              ring_push(sDepth[c->playerNum].deltas, dt)   // JITTER_WINDOW ring
          sDepth[c->playerNum].lastArrivalPC = now
          Queue_Push(c->playerNum, c)                      // 128-slot ring
      // else: drop malformed/OOB (wire-safety)
  else:
      HandleOtherNetMessage(m)          // PlayerLeft/pause/etc -> existing dispatcher
  NSpMessage_Release(gNetGame, m)

### Host_DeriveEdges(prevBits, newBits)  -> engine formula, InputControlBits.c:145
return (newBits ^ prevBits) & newBits

### Host_ConsumeClientInputs()  (top of host frame, before HostSend, never blocks > GRACE)
memset(sInputFlags, 0, sizeof sInputFlags)

// --- single bounded GRACE phase (only place host may sleep; total <= GRACE_RETRIES ms) ---
for g in 0..GRACE_RETRIES-1:
    anyEmpty = false
    for i in active remote clients: if Queue_Count(i)==0: anyEmpty=true
    if !anyEmpty: break
    SDL_Delay(1); Net_Pump()            // re-drain near-miss kernel-buffered packets

for i in 0..MAX_PLAYERS-1:
    if i==gMyNetworkPlayerNum: continue            // host reads own input at Step 3
    if gPlayerInfo[i].isComputer: continue          // bot/dropped -> AI drives bits
    S = &sLastApplied[i]; D = sDepth[i].targetDepth

    // discard stale (dups / pre-roll already consumed); preserves old <expected semantics
    while ((p=Queue_Peek(i)) && p->inputSeq <= S->lastAppliedSeq): Queue_Pop(i)
    B = Queue_Count(i)

    if B == 0:                                       // ---- SUBSTITUTE (underrun) ----
        gPlayerInfo[i].controlBits     = S->valid ? S->controlBits : 0
        gPlayerInfo[i].controlBits_New = 0           // no new edges while silent
        gPlayerInfo[i].analogSteering  = S->valid ? S->analogSteering : (OGLVector2D){0,0}
        gPlayerInfo[i].net.pauseState  = S->valid ? S->pauseState : 0   // HELD, never unpaused
        S->subStreak++
        if S->subStreak > SUB_DECAY_AFTER:           // graceful coast: decay APPLIED ONLY
            gPlayerInfo[i].analogSteering = lerp(gPlayerInfo[i].analogSteering, {0,0}, 0.1)
            gPlayerInfo[i].controlBits &= ~(MOVEMENT_BITS)  // clear accel/brake/steer bits
            // IMPORTANT: do NOT touch S->controlBits -> edge baseline stays = last REAL bits,
            // so a still-held button does not re-fire as a NEW edge on recovery
        sInputFlags[i] |= INPUT_FLAG_SUBSTITUTED
        sDepth[i].targetDepth = min(8, D + 1)        // immediate underrun bump
        // do NOT advance lastAppliedSeq; ack stays = last REAL applied
        continue

    if B > D + 1:                                    // ---- COALESCE down to D ----
        toPop = B - D
        prev = S->controlBits; merged = 0; NetClientControlInfoMessageType last
        for k in 0..toPop-1:
            p = Queue_Peek(i)
            merged |= Host_DeriveEdges(prev, p->controlBits)   // preserve EVERY press
            prev = p->controlBits; last = *p; Queue_Pop(i)
        gPlayerInfo[i].controlBits     = last.controlBits
        gPlayerInfo[i].controlBits_New = merged
        gPlayerInfo[i].analogSteering  = last.analogSteering
        gPlayerInfo[i].net.pauseState  = last.pauseState
        commitApplied(S, &last); sInputFlags[i] |= INPUT_FLAG_COALESCED
    else:                                            // ---- APPLY exactly one ----
        p = Queue_Peek(i)
        gPlayerInfo[i].controlBits     = p->controlBits
        gPlayerInfo[i].controlBits_New = Host_DeriveEdges(S->controlBits, p->controlBits)
        gPlayerInfo[i].analogSteering  = p->analogSteering
        gPlayerInfo[i].net.pauseState  = p->pauseState
        commitApplied(S, p); Queue_Pop(i)

    Host_UpdateDepth(i)                              // decay path (below)

commitApplied(S, m):
    S->controlBits=m->controlBits; S->analogSteering=m->analogSteering
    S->pauseState=m->pauseState;   S->lastAppliedSeq=m->inputSeq
    S->valid=true; S->subStreak=0
    gClientSendCounter[i] = m->inputSeq + 1          // bookkeeping mirror

### Host_UpdateDepth(i)  (recompute baseline <=4Hz, decay slowly)
if now - sDepth[i].baselineFrameTick >= 0.25s:
    tmp[] = |deltas[k] - F| for k in 0..count-1; F = 1/gTargetFPS (or 1/60)
    sort(tmp); P95 = tmp[ceil(0.95*count)-1]
    Dbase = clamp(ceil(P95 / F) + 1, 1, 8)
    if Dbase > targetDepth: targetDepth = Dbase
    sDepth[i].baselineFrameTick = now
// decay 1 frame per 2s while not recently bumped and P99 margin >= 1 frame
sDepth[i].decayAccumSec += F
if decayAccumSec >= 2.0 && targetDepth > Dbase && targetDepth > 1:
    targetDepth--; decayAccumSec = 0

### SampleAndSendLocalInput(outSchedulePause)  (client Step 3; G1)
now = perfNow(); F = (gTargetFPS>0)? 1.0/gTargetFPS : 1.0/60.0
if sNextSampleTime==0 || now - sNextSampleTime > 0.250: sNextSampleTime = now  // suspend resync
burst = 0
while now >= sNextSampleTime && burst < MAX_SAMPLE_BURST:
    ReadKeyboard(); GetLocalKeyState()                          // Main.c:1077-1078 machinery
    *outSchedulePause |= GetNewNeedStateAnyP(kNeed_UIPause)     // OR across burst (edge-safe)
    gPlayerInfo[gMyNetworkPlayerNum].net.pauseState = *outSchedulePause
    ClientSend_ControlInfoToHost()                              // inputSeq = gClientSendCounter[me]++
    sNextSampleTime += F; burst++; now = perfNow()
// NOTE Stage 2: client is still gated by ClientReceive (1 pkt/frame), so this normally
// emits ~1/frame; after a resolved downlink stall it bursts up to 3 to refill the host's
// uplink backlog. Full uplink-during-downlink-spike decoupling completes in Stage 3.

### Main.c host frame ordering (replaces blocking model)
Step1(host):  Net_Pump(); Host_ConsumeClientInputs(); HostSend_ControlInfoToClients();
Step2:        simulate (unchanged)
Step3(host):  ReadKeyboard(); GetLocalKeyState(); schedulePause=...; pauseState=...
Step3(client):SampleAndSendLocalInput(&schedulePause); Net_Pump()/FlushSends();
Step4/5:      render + spin-cap (unchanged); end-of-frame HostReceive DELETED


**Edge cases:**
- First race frame, no real input yet (valid==false): substitution holds zero bits / neutral analog / unpaused. The D_init pre-roll duplicates should give a backlog so the very first applied frame is real; the zero path is only a guard.
- All queued packets stale (seq <= lastAppliedSeq, e.g. leftover pre-roll dups): the discard-stale loop empties the queue -> treated as underrun -> substitute. Never apply a packet older than the last real one.
- Decay-on-long-substitution MUST decay only the APPLIED gPlayerInfo[i].controlBits/analog, never sLastApplied.controlBits. Otherwise a still-held button reappears as a NEW edge on recovery and double-fires (e.g. weapon throw). Edge baseline = last REAL bits.
- Substitution must HOLD pauseState (copy last real), never zero it: a radio blip cannot spuriously unpause the game. Conversely bots/dropped players are still forced pauseState=0 in HostSend (822-825).
- Pause is a level field (net.pauseState), not a controlBit edge. Across a coalesce window the NEWEST popped packet's pauseState wins; a press+release entirely inside one D*F window can be missed (accepted, document) but a normal toggle survives because it persists in subsequent packets.
- controlBitsNew is derived ONLY on the host; clients apply mess->controlBitsNew verbatim (NetHigh.c:885). No client re-derivation -> no cross-machine edge divergence -> seed check stays valid.
- Host's own slot and AI/bot slots are skipped by Host_ConsumeClientInputs (their controlBits come from GetLocalKeyState at Step 3 / AI). Guard with i==gMyNetworkPlayerNum and isComputer.
- Coalesce when B>D+1 but the surviving newest is the only non-stale: stale-discard first, recompute B, then it falls into single-apply.
- D_i is clamped [1,8]; queue is 128 slots, so D_max=8 can never overflow. Queue_Push still drops on full ring (>~2s backlog) -> that client is starving; Stage 1 ring/kick handles send pressure, here it just means deep underrun.
- gClientSendCounter / inputSeq are u32 monotonic; TCP ordering guarantees no reordering, so a simple seq comparison is sufficient (no wrap concern within a race).
- Wall-clock sampler after app suspend / >250ms wall gap: resync sNextSampleTime=now and cap burst at 3, preventing a flood of stale-input packets.
- GetNewNeedStateAnyP is edge-triggered; calling it multiple times in one burst would drop the edge on later calls -> OR the result into outSchedulePause so the local pause menu still triggers.
- Second hosted game in the same process: ResetNetGameTransientState() must zero sHostInputQueues/sLastApplied/sDepth/gTimeoutCounter/counters or the host wedges on stale future-seq packets (the existing latent bug).
- Mixed-link race: c1 wired converges to D=1 (substitution/coalesce counters ~0), c2 WiFi to D=2-8; one client's depth/substitution must never affect another's apply path.


**Testing.** Build: confirm _Static_assert on every wire struct passes on x86_64 and one ARM target; confirm a CMR6 binary is cleanly rejected (NetLow.c:718-723 logs 'bad protocol', no crash). Verify sizeof(NetHostControlInfoMessageType) and NetClientControlInfoMessageType match across platforms (print at startup). Harness = 3 network namespaces (cmr_h/cmr_c1/cmr_c2) over veth+bridge, launched via --host/--join/--port (Main.c:1823-1842), impaired with tc netem. Profile A (clean): 20-race all-bot soak, assert zero kNetSequence_SeedDesync, zero fatals, second-game-in-process starts clean. Profile C (200ms uplink bursts every 20-60s on c2): host frame-time histogram stays flat (no stall >50ms attributable to c2) while c2's car coasts hold-last then rubber-bands; HUD shows substitutions/min spike and D_i bump->decay. Profile E (acceptance: c1 clean, c2 profile D): assert wired-client (c1) frame-time p99 <= 1.2F and own-input delay <=40ms via (ownSeq-ackInputSeq)*F + halfRTT; c2 D_i inflates to 4-8 and decays. Coalesce/edge unit test: script c2 to fire a kControlBit_ThrowForward press straddling a coalesced window; assert the merged controlBitsNew on the host (and echoed to all) contains that edge exactly once -> no lost press, no double press. Pause tests: client toggles pause inside a coalesce window (assert sim freezes everywhere); inject an uplink gap WHILE paused (assert it stays paused, no spurious unpause from substitution). Determinism: x86-host / ARM-client full race under profile B with catch-up bursts, assert seed check passes every frame. A/B: build with CMR7_SEND_PER_FRAME to confirm the sampler can be reverted. Metrics emitted to CSV + debug HUD per second: per-client D_i, queueDepth, substitutions/min, multi-frame-substitution events, coalesce events, max delivery gap, ack input delay, frame-time p50/p95/p99, seed-check pass count.


**Risks:**
- HARD DEPENDENCY on Stage 1: Net_Pump, SendOrEnqueue/NSpGame_FlushSends, and per-socket 32KB send rings must exist. If Stage 1 is not merged, Stage 2 must ship a minimal Net_Pump (drain) + non-blocking send first, or the deleted blocking HostReceive leaves the host with no input intake.
- Stage 2 client still BLOCKS in ClientReceive (1 pkt/frame, with the Stage 0g SDL_Delay(1) sleep). So the wall-clock sampler's full G1 benefit (uplink alive during a downlink spike) is only PARTIALLY realized here (it bursts to catch up only after the stall resolves). The host-side stutter win is complete and shippable; the client-side decoupling lands in Stage 3 - state this in the changelog so the win is not over-claimed.
- Wire ABI: uint8_t arrays adjacent to float/OGL arrays can pad differently across the 6 platforms. Group same-alignment fields, add exact-size _Static_asserts, and do the 256->512 cap bump in the SAME commit as the 4CC bump (risk 7).
- Coalescing merges same-button double-taps within <= D*F (33-130ms) into one edge. Host per-step derivation minimizes loss and strictly beats today's silent edge drop, but add explicit pause-edge-ordering unit tests across coalesced packets (risk 2).
- Adaptive-depth oscillation on bursty links (bump-on-underrun vs slow decay): clamp [1,8], decay 1 frame/2s, recompute P95 baseline <=4Hz, and only decay below the live P95 baseline to avoid thrash.
- Long-substitution decay (>30 frames) could make a coasting car feel limp, and naive decay of the edge baseline would double-fire held buttons on recovery - mitigated by decaying ONLY the applied bits, never sLastApplied. Tune SUB_DECAY_AFTER in playtests.
- Retiring gUseRedundancy touches several call sites (NetHigh.c 491/629/675/1069/1103/1177, Main.c 1008); the dead reads are in deleted blocks, but verify no other module keys off it (grep confirms only game.h decl + these). Per-client sClientConnectionHint must be populated before Host_InitInputControl runs.
- Cross-arch float divergence is exercised harder by coalesce/substitution-driven trajectories; rubber-band absorbs position, seed check only guards RNG draw count. Keep -ffp-contract=off recommendation; do NOT deadband the 0.2 rubber-band (risk 6).
- ResetNetGameTransientState must be wired into BOTH end and host-init paths or the second-game-in-process wedge (and gTimeoutCounter-class regressions) return (risk 11).


## Stage 3 — Client free-running receive: 32-slot host-packet ring + bounded catch-up (K_max) + hold-last-frame

**Effort:** "4-5 dev-days. Independently shippable ON TOP OF Stages 1+2 (needs Net_Pump/SendOrEnqueue from Stage 1 and the host top-of-frame restructure + CMR7 structs + wall-clock SampleAndSendLocalInput from Stage 2). Breakdown: ring + pump/consume primitives + ClientReceive shim ~1d; Main.c StepGameSimulation factoring + client catch-up loop + hold-last + render decoupling ~1.5d; dead-code/decl cleanup + EndNetworkGame reset ~0.25d; netns downlink-spike validation + K_max Android profiling + cross-arch soak + telemetry wiring ~1.5-2d. Hard same-release dependency: Stage 4 must convert Paused.c/loading barriers off the blocking shim."

**Objective.** Eliminate the client's zero-sleep blocking lockstep so the client never freezes on a late/absent host packet and can drain a post-stall burst. Replace the busy-wait ClientReceive_ControlInfoFromHost (NetHigh.c:939-1001, called at Main.c:1040) with: (1) a non-blocking pump that drains all readable host control packets into a 32-slot ordered ring; (2) a bounded per-render-frame catch-up loop that consumes up to K_max=3 packets, applying each via the UNCHANGED Client_InGame_HandleHostControlInfoMessage (NetHigh.c:879-937) and stepping the simulation once per applied packet (bit-identical trajectory because each backlogged packet carries and replays its own host dt); (3) hold-last-frame when the ring is empty (re-present the previous frame, surface a subtle net badge after 250ms continuous absence). After this stage a client downlink spike disturbs only that client (it holds then catches up at net +2 sim-frames/render-frame) and is invisible to every other machine (relies on the Stage-2 G1 wall-clock uplink sender keeping that client's input stream alive). Also delete the dead Client_CheckIfMorePacketsWaiting (NetHigh.c:1008-1049, decl network.h:112) and factor Main.c's inline sim block (1052-1069) into a reusable StepGameSimulation(). DEPENDENCIES: Stage 1 (Net_Pump + SendOrEnqueue send rings) and Stage 2 (host top-of-frame restructure, CMR7 wire structs, wall-clock SampleAndSendLocalInput, net-only dt clamp 2/gTargetFPS) MUST be in place; Stage 4 converts Paused.c/loading barriers off the retained blocking shim and is a hard same-release dependency (otherwise blocking waits return via the pause menu).


**Changes:**

- `Source/Headers/network.h` (network.h:112 (decl Client_CheckIfMorePacketsWaiting)) — DELETE the Client_CheckIfMorePacketsWaiting prototype. ADD prototypes used by Main.c/Net_Pump/Paused: void Client_PumpHostPackets(void); int Client_ConsumeHostPacketFromRing(void); Boolean Client_IsHoldBadgeVisible(void); void ResetClientHostRing(void). Keep the existing ClientReceive_ControlInfoFromHost decl (line 111) — it is retained as a bounded compatibility shim for Paused.c/barriers until Stage 4.
- `Source/Network/NetHigh.c` (after Client_InGame_HandleHostControlInfoMessage (insert ~938, before ClientReceive at 939)) — ADD client host-packet ring: HostPacketRing struct + static sClientHostRing, plus HostRing_Count/Full/Push/Pop helpers, plus telemetry statics (sLastHostArrivalTick, sClientHoldFrames). ADD Client_PumpHostPackets() (drains the socket into the ring, routes non-control messages to HandleOtherNetMessage, stops draining when ring full to apply TCP backpressure — NEVER drops a host control packet). ADD Client_ConsumeHostPacketFromRing() (pop one, call the verbatim handler, return Applied/Dup/Empty). ADD Client_IsHoldBadgeVisible()/ResetClientHostRing().
- `Source/Network/NetHigh.c` (Client_InGame_HandleHostControlInfoMessage, 879-937) — KEEP VERBATIM. Do not touch the dup-drop, lost-packet fatal, dt adoption (870-871), fatal seed check (876-880-equiv), input copy, or the 0.2 rubber-band. The ring/catch-up wraps this function; it is the single point that advances gHostSendCounter and applies a host frame.
- `Source/Network/NetHigh.c` (ClientReceive_ControlInfoFromHost, 939-1001) — REPURPOSE as a bounded blocking shim used ONLY by Paused.c/barriers (no longer called from the in-game loop): loop { Net_Pump(); r=Client_ConsumeHostPacketFromRing(); if Applied return; if Dup continue; pump SDL events + SDL_Delay(1); apply lastHeard/timeout policy }. DELETE the zero-sleep spin and the #if 0 resend block. It must only APPLY one host packet (advance counter/inputs/rubber-band) and must NOT call StepGameSimulation — the pause callback drives its own MoveObjects.
- `Source/Network/NetHigh.c` (Client_CheckIfMorePacketsWaiting, 1008-1049) — DELETE the entire function (dead code, zero call sites after the loop rewrite).
- `Source/Network/NetHigh.c` (EndNetworkGame, 188-224 (and Stage-0 ResetNetGameTransientState if present)) — Call ResetClientHostRing() (zero head/tail, clear hold timers) so a second net game in the same process starts with an empty client ring. Co-locate with the Stage-0/Stage-1 transient resets.
- `Source/Network/NetHigh.c` (Net_Pump (new in Stage 1, extended in Stage 2)) — In the client role branch of Net_Pump, call Client_PumpHostPackets() to drain host control packets into the ring (alongside the Stage-1 send-ring flush). This is the only in-game client socket-drain site.
- `Source/System/Main.c` (static prototypes block near Main.c:45 (UpdateGameModeSpecifics decl)) — ADD prototype: static void StepGameSimulation(void);
- `Source/System/Main.c` (PlayArea sim block, 1052-1069) — EXTRACT verbatim into static void StepGameSimulation(void) { if(IsNetGamePaused()){gSimulationPaused=true;SetupNetPauseScreen();MoveObjects();DoPlayerTerrainUpdate();} else {gSimulationPaused=false;RemoveNetPauseScreen();MoveEverything();UpdateGameModeSpecifics();DoPlayerTerrainUpdate();gSimulationFrame++;} }. Single-player and host call this once per frame in place of the inline block (behavior unchanged).
- `Source/System/Main.c` (PlayArea Step 1 client branch, 1036-1041 (ClientReceive call at 1040) + extracted Step 2) — For the client: REMOVE the blocking ClientReceive call and the standalone single StepGameSimulation. Replace with Net_Pump() then the bounded catch-up loop (see algorithms). Host and single-player keep exactly one StepGameSimulation() call. gClientCatchUpMax(=K_max) is a tunable int (default 3, Android fallback 2).
- `Source/System/Main.c` (PlayArea render, Step 4 at 1090 (OGL_DrawScene(DrawTerrain))) — Leave the render call UNCONDITIONAL — on a hold frame the scene is unchanged so OGL_DrawScene re-presents the previous image (this IS hold-last-frame). Optionally draw a subtle stall badge when Client_IsHoldBadgeVisible() (minimal in Stage 3; Stage 4 unifies it with the lastHeard badge).
- `Source/System/Main.c` (PlayArea Step 3 send, 1075-1084 (ClientSend at 1083)) — No new work in Stage 3: the client uplink is the Stage-2 wall-clock SampleAndSendLocalInput() (decoupled from packet consumption — the G1 property this stage relies on). Verify schedulePause is still derived from the local input read for the Main.c:1134/1149 pause-menu paths.
- `Source/Screens/Paused.c` (UpdatePausedMenuCallback, 155-157) — No code change in Stage 3 — keeps calling the retained (now non-spinning) ClientReceive shim + ClientSend. Tracking note: Stage 4 re-points this to Net_Pump/consume + SampleAndSendLocalInput and fixes the Paused.c:156 bail-on-death TODO. Must convert same release (risk 9).


**Data structures.** // ---- Source/Network/NetHigh.c ----

// Client downlink staging ring of full host control packets.
// 32 slots ~= 530ms of host frames @60fps. Ordered; never drops in the middle.
#define CLIENT_HOST_RING_SIZE 32

typedef struct
{
    int                              head;   // next slot to consume
    int                              tail;   // next slot to fill
    NetHostControlInfoMessageType    slots[CLIENT_HOST_RING_SIZE];
} HostPacketRing;                            // empty: head==tail; full: (tail+1)%N==head

static HostPacketRing sClientHostRing;       // static => zero-init; reset in EndNetworkGame

// Result of consuming one ring entry.
typedef enum
{
    kHostConsume_Empty   = 0,                // ring empty
    kHostConsume_Dup     = 1,                // popped but handler dropped it (old/dup) — no sim step
    kHostConsume_Applied = 2,                // popped + applied (counter advanced) — caller steps sim
} HostConsumeResult;

// Hold-last / badge telemetry (TickCount() is 60Hz Pomme ticks).
static uint32_t sLastHostArrivalTick = 0;    // tick of most recent host packet PUSHED to ring
static uint32_t sClientHoldFrames    = 0;    // consecutive render frames that stepped 0 sims (telemetry)

#define CLIENT_HOLD_BADGE_TICKS  15          // 250ms @60Hz: continuous absence => show badge

// ---- Source/System/Main.c ----
// K_max catch-up bound. Tunable; Android weakest-target fallback = 2 (terrain streaming cost).
int gClientCatchUpMax = 3;                   // (extern int in game.h if a menu/CLI tunes it)


**Algorithms.** RING HELPERS (NetHigh.c):
  HostRing_Count(): return (tail - head + N) % N
  HostRing_Full():  return ((tail + 1) % N) == head
  HostRing_Push(msg): if HostRing_Full() return false; slots[tail]=*msg; tail=(tail+1)%N; return true
  HostRing_Pop(out): if head==tail return false; *out=slots[head]; head=(head+1)%N; return true
  ResetClientHostRing(): head=tail=0; sLastHostArrivalTick=TickCount(); sClientHoldFrames=0

Client_PumpHostPackets()  // called from Net_Pump (client role); drains socket -> ring
  GAME_ASSERT(gIsNetworkClient)
  while (!HostRing_Full()):                 // CRITICAL: only Get() while we can store it
      inMess = NSpMessage_Get(gNetGame)     // consumes from socket (recv)
      if (inMess == NULL) break             // socket drained
      if (inMess->what == kNetHostControlInfoMessage):
          HostRing_Push((NetHostControlInfoMessageType*)inMess)   // guaranteed space
          sLastHostArrivalTick = TickCount()                     // arrival time for badge
      else:
          HandleOtherNetMessage(inMess)     // PlayerLeft / GameTerminated / sync / etc.
      NSpMessage_Release(gNetGame, inMess)
  // If ring is full we STOP draining: unread bytes stay in the 64KB kernel buffer
  // (TCP backpressure). We never drop a host control packet -> no gap -> no false LostPacket.

Client_ConsumeHostPacketFromRing() -> HostConsumeResult  // pop + APPLY one (does NOT step sim)
  NetHostControlInfoMessageType pkt
  if (!HostRing_Pop(&pkt)) return kHostConsume_Empty
  applied = Client_InGame_HandleHostControlInfoMessage(&pkt)   // VERBATIM 879-937
  return applied ? kHostConsume_Applied : kHostConsume_Dup     // false => old/dup; may also fatal inside

StepGameSimulation()  // Main.c, extracted verbatim from 1052-1069 (used by host, client-per-packet, SP)
  if IsNetGamePaused():
      gSimulationPaused=true;  SetupNetPauseScreen();  MoveObjects();  DoPlayerTerrainUpdate()
  else:
      gSimulationPaused=false; RemoveNetPauseScreen(); MoveEverything(); UpdateGameModeSpecifics();
      DoPlayerTerrainUpdate(); gSimulationFrame++

PLAYAREA INNER LOOP — CLIENT BRANCH (replaces Main.c Step-1 client + standalone Step-2):
  Net_Pump()                                 // flush send rings (Stage1) + Client_PumpHostPackets()
  stepped = 0
  guard = 0
  for (k = 0; k < gClientCatchUpMax; ):      // bounded catch-up (K_max=3)
      r = Client_ConsumeHostPacketFromRing()
      if (r == kHostConsume_Applied): StepGameSimulation(); stepped++; k++
      elif (r == kHostConsume_Dup):
          if (++guard > 2*gClientCatchUpMax) break   // defense vs pathological dup storm
          continue                            // dup: no sim, do not burn a k-slot
      else break                              // kHostConsume_Empty
  if (stepped == 0):
      sClientHoldFrames++                     // HOLD-LAST: render unchanged scene at Step 4
  else:
      sClientHoldFrames = 0
  // schedulePause + uplink come from Stage-2 SampleAndSendLocalInput() at Step 3 (unchanged here).
  // Step 4 OGL_DrawScene runs UNCONDITIONALLY; on a hold frame it re-presents the prior image.

Client_IsHoldBadgeVisible():                 // subtle net icon after 250ms continuous absence
  if (gNoCarControls) return false           // suppress during start-light countdown
  return (TickCount() - sLastHostArrivalTick) > CLIENT_HOLD_BADGE_TICKS

ClientReceive_ControlInfoFromHost()  // RETAINED bounded shim — Paused.c / loading barriers ONLY
  GAME_ASSERT(gIsNetworkClient)
  tick = TickCount()
  loop:
      Net_Pump()                             // drains into ring + flush sends
      r = Client_ConsumeHostPacketFromRing()
      if (r == kHostConsume_Applied) return  // applied exactly one host frame; caller (pause cb) does MoveObjects
      if (r == kHostConsume_Dup) continue
      // empty: yield instead of zero-sleep spin (Stage-0g behaviour)
      PumpSDLEvents()                         // keep window responsive
      if ((TickCount() - tick) > DATA_TIMEOUT*60): apply lastHeard/timeout policy; return
      SDL_Delay(1)


**Edge cases:**
- Ring overflow must NEVER drop a host control packet: dropping a middle packet makes the next one trip the handler's lost-packet fatal (frameCounter>gHostSendCounter). Enforce drain-only-when-ring-has-space + kernel/TCP backpressure (host send-ring overflow -> existing Stage-1 kick). Telemetry counter on sustained full-ring.
- Duplicate/old packet: handler returns false (drops by frameCounter<gHostSendCounter) -> kHostConsume_Dup. Loop continues WITHOUT advancing sim or burning a k-slot, with a hard 2*K_max guard. TCP never duplicates; defense-in-depth.
- Persistent backlog (render rate < host packet rate on a weak client): ring stays near-full; client runs a few frames behind but correct and never fatal. Telemetry flags it. Not a Stage-3 failure.
- Genuine lost host packet: impossible over ordered TCP unless we dropped one (forbidden). Keep the handler's lost-packet fatal as the canary.
- Hold at start-light: at the gun the client may have 0 host packets (pre-roll deleted; Stage-2 D_init duplicates seed the host, not the client downlink). It holds until first arrival; suppress badge while gNoCarControls.
- Badge keyed on ARRIVAL (sLastHostArrivalTick), not stepped==0: a frame that only popped dups still 'heard' the host -> no badge. Prevents false stall indication.
- Pause over net: IsNetGamePaused() routes StepGameSimulation to the paused branch; host keeps streaming during pause so the client consumes+steps and counters stay aligned. DoPaused menu uses the retained shim (apply-one, NO StepGameSimulation) -> must not double-step.
- Catch-up dt: each applied packet replays its own host dt (870-871), bounded by Stage-2's clamp 2/gTargetFPS (~33ms). 3 applied => up to ~100ms sim in one render frame. Without the Stage-2 clamp a stale 111ms (DEFAULT_FPS=9) dt could inject one violent step -> Stage-3 depends on the clamp.
- K_max terrain cost: StepGameSimulation calls DoPlayerTerrainUpdate (supertile streaming) up to 3x/render-frame; weakest Android may exceed budget -> stutter during catch-up. Profile early; fall back gClientCatchUpMax=2.
- Render/sim decoupling: was exactly one sim step per render, now 0..K_max. Audit render-side consumers that assumed one MoveEverything per OGL_DrawScene (HUD timers on gFramesPerSecondFrac, fade events, skid/particle spawns); hold frame retains last gFramesPerSecondFrac but runs no sim.
- Second net game in same process: ResetClientHostRing() from EndNetworkGame clears stale slots/timers (mirrors the Stage-0 jitter-queue/counter reset bug class).
- Non-control message (PlayerLeft/GameTerminated) stuck behind a full ring is delayed until the ring drains (bounded by K_max). Document; acceptable for v1.
- Seed desync mid-catch-up: handler still fatals (kept); diagnostics extended in Stage 2 — unchanged here.


**Testing.** "netns harness (cmr_h/cmr_c1/cmr_c2 + tc-netem), bot-driven races. PRIMARY: profile C (spiky 5GHz + scripted 200ms DOWNLINK bursts on c1's veth via `tc qdisc change ... delay 200ms` toggled every 20-60s). ASSERT (a) other-machine invisibility/G1: during c1's 200ms downlink gap, c2 and host frame-time p99 show NO stall >50ms attributable to c1 (HUD histogram + CSV); (b) affected-client recovery: c1 hold-event <=~200ms then backlog clears in <=300ms total (~12 frames cleared in ~6 render frames at K_max=3); (c) zero kNetSequence_ErrorLostPacket and zero kNetSequence_SeedDesync (ring ordering correctness). DETERMINISM: profile A 20-race soak, zero desync/zero spurious fatal + bit-identical trajectory vs an unimpaired control run. K_MAX PROFILING (risk 5): on the weakest Android, instrument StepGameSimulation x3 wall-time; if render-frame p99 exceeds budget set gClientCatchUpMax=2 and re-verify the backlog still fully clears. PAUSE: pause during a profile-C race; confirm the retained shim does not spin (low CPU, responsive window) and counters realign on resume. CROSS-ARCH (risk 6): x86 host + ARM client repeat profile A/B (catch-up exercises FP hardest) — assert seed check passes. METRICS to CSV+HUD: ring depth/high-water, hold events/min + duration histogram, catch-up steps per render frame, max downlink gap ms, frame-time p50/p95/p99, seed-check pass count."


**Risks:**
- K_max=3 catch-up cost (3x DoPlayerTerrainUpdate supertile streaming per render frame) may blow the frame budget on the weakest Android during a burst -> micro-stutter. Mitigation: profile in Stage 3; fall back gClientCatchUpMax=2 (slower but complete recovery).
- A ring-overflow bug that drops a middle host packet causes an immediate false kNetSequence_ErrorLostPacket fatal. The drain-only-when-space + backpressure invariant is load-bearing; unit-test a ring flood asserting no packet is ever discarded and counter continuity holds.
- Paused.c and loading barriers still call the retained ClientReceive shim. If it regresses (zero-sleep spin, accidentally steps the sim, or applies !=1 packet/menu-frame) the pause path desyncs counters or refreezes. Stage 4 conversion is a hard same-release dependency (synthesis risk 9).
- Hold-last-frame v1 shows a frozen image during >1-frame downlink gaps (no extrapolation) — accepted (today's behavior is identical but global). Stage 6 upgrades to transform-rebuilt extrapolation; do not attempt naive Coord/Rot save-restore here (renders nothing without UpdateObjectTransforms).
- Catch-up bursts exercise x86<->ARM FMA float divergence harder than steady play; rubber-band absorbs position drift but the seed check only guards RNG draw count. Pin -ffp-contract=off and soak cross-arch; do NOT deadband the rubber-band.
- Render/sim decoupling (0..K_max steps per render) can double- or zero-count render-side logic that assumed one sim step per frame (HUD timers, fades, skid/particle emission). Requires an audit of OGL_DrawScene/DrawTerrain consumers of gFramesPerSecondFrac and per-frame spawns.
- Non-control messages (PlayerLeft/GameTerminated) can be delayed behind a full ring, slightly delaying leave/termination handling on a badly-backlogged client. Bounded by ring/K_max; revisit if Stage 4 leave-timing tests show it.
- Depends on Stage 2's net-only dt clamp (2/gTargetFPS); without it a backlogged packet could carry a 111ms (DEFAULT_FPS=9) dt and inject one violent physics step during catch-up.


## Stage 4 — Policy: timeouts, pause, frame-aligned leave/bot-conversion, FPS negotiation, mobile radio

**Effort:** 3-4 dev-days. Breakdown: lastHeard plumbing + badge/drop policy + keepalive ~1d; frame-aligned become-bot scheduler/applier + tag/pause/double-leave tests ~1.5d; FPS finalize-and-broadcast + pause-callback rewrite + CancelMenu ~0.5-1d; Android WifiLock + JNI fallback ~0.25d. NOT independently shippable on bare master — gated behind Stages 2 and 3 (calls Net_Pump/Host_ConsumeClientInputs/Client_PumpHostPackets/SampleAndSendLocalInput and the CMR7 wire bump). Ships in the same release train as Stage 3 (risk 9).

**Objective.** Replace the session-killing 4-strike DATA_TIMEOUT with a per-connection lastHeard badge(>1s)/drop(>10s) policy; make player departures deterministic by converting the leaving player to a bot frame-aligned across all 6 machines (G3) instead of at TCP-arrival time (fixes the ChooseTaggedPlayer RNG-draw desync); re-point the pause menu and the loading/lobby barriers at the Stage 2/3 non-blocking pump+sampler path and add a 20Hz 'keep' heartbeat so WiFi radios never enter power-save and lastHeard stays fresh outside in-game streaming; fix the asymmetric gTargetFPS LCD negotiation by having the host finalize the value after vehicle-select and broadcast it once in the level-prep kNetSyncMessage; and acquire an Android WIFI_MODE_FULL_LOW_LATENCY WifiLock. HARD DEPENDENCY: Stages 2 and 3 must already be merged — this stage calls Net_Pump / Host_ConsumeClientInputs / Client_PumpHostPackets / SampleAndSendLocalInput and relies on the CMR7 wire bump (event block + NetSyncMessage.targetFPS). It is NOT independently shippable on bare master (risk 9).


**Changes:**

- `Source/Headers/network.h` (message enum at network.h:11-16) — Add kNetKeepAliveMessage = 'keep' to the message-type enum. Heartbeat carries no payload beyond the 28-byte NSpMessageHeader.
- `Source/Headers/network.h` (NetSyncMessage struct at network.h:41-44) — Add `uint16_t targetFPS;` (and 2 bytes pad) to NetSyncMessage so the host's level-prep sync carries the finalized LCD framerate. Add _Static_assert(sizeof(NetSyncMessage)==...) (this field is part of the Stage 2 CMR7 wire bump; add it here if Stage 2 did not).
- `Source/Headers/network.h` (NetHostControlInfoMessageType at network.h:49-63) — Confirm the Stage-2 event block exists: `uint8_t eventCount; NetEvent events[2];` plus the NetEvent struct + kEv* enum (see dataStructures). If Stage 2 only reserved the bytes, define the NetEvent type and enum here. No new wire bytes added by Stage 4.
- `Source/Headers/netsprocket.h` (NSpPlayer struct at netsprocket.h:65-71) — Add `uint32_t lastHeard;` (ms from SDL_GetTicks) — host tracks per-client last-received-bytes time. Not a wire field; pure in-memory.
- `Source/Headers/netsprocket.h` (NSpGame struct at netsprocket.h:73-90) — Add `uint32_t hostLastHeard;` — client tracks last-received-bytes time from host. In-memory only.
- `Source/Network/NetLow.c` (NSpMessage_GetAsHost, success branch at NetLow.c:816-832) — On any non-NULL message from a client socket, set player->lastHeard = SDL_GetTicks() (covers 'keep' and all real traffic). Also initialize lastHeard=SDL_GetTicks() in NSpGame_AcceptNewClient when a player slot becomes active.
- `Source/Network/NetLow.c` (NSpMessage_GetAsClient at NetLow.c:838-862) — On any non-NULL message from clientToHostSocket, set game->hostLastHeard = SDL_GetTicks(). Initialize hostLastHeard at join time (JoinLobby success).
- `Source/Network/NetHigh.c` (#define DATA_TIMEOUT at NetHigh.c:45) — Delete DATA_TIMEOUT and all gTimeoutCounter usage (incl. resets at 857). Add #define NET_BADGE_MS 1000, NET_DROP_MS 10000, NET_KEEPALIVE_MS 50, EV_BECOME_BOT_LEAD (D_MAX+4), D_MAX 8.
- `Source/Network/NetHigh.c` (new function NetCheck_ConnectionTimeouts() near the pump code) — Per-frame policy: host scans players[]; (now-lastHeard)>NET_BADGE_MS sets gNetBadge[playerNum]=true, >NET_DROP_MS schedules kEvBecomeBot (same path as leave) once. Client checks hostLastHeard: >NET_BADGE_MS sets badge, >NET_DROP_MS -> NetGameFatalError(kNetSequence_ErrorNoResponseFromHost). Called once/frame from Net_Pump (host) and from the client frame top.
- `Source/Network/NetHigh.c` (new function Net_MaybeSendKeepAlive() + static gLastNetSendMs) — If (SDL_GetTicks()-gLastNetSendMs) >= NET_KEEPALIVE_MS, send a header-only kNetKeepAliveMessage (host: to kNSpAllPlayers; client: to kNSpHostID). All SendOrEnqueue paths bump gLastNetSendMs so in-game streaming suppresses redundant keepalives.
- `Source/Network/NetHigh.c` (HandleOtherNetMessage at NetHigh.c:1417-1503) — Add `case kNetKeepAliveMessage: break;` (no-op; lastHeard already refreshed). Change `case kNSpPlayerLeft` (1432-1438): in kNetSequence_GameLoop call ScheduleBecomeBotFromLeave(mess) instead of PlayerUnexpectedlyLeavesGame() immediately; outside GameLoop (lobby/barriers) keep the immediate call (no running sim to desync).
- `Source/Network/NetHigh.c` (PlayerUnexpectedlyLeavesGame at NetHigh.c:1513-1545) — Rename body into ApplyBecomeBot(int playerNum) operating on gPlayerInfo[playerNum] directly (drop the NSpID lookup; the event carries dense playerNum). Keep isComputer/isEliminated/pauseState=0/gNumGatheredPlayers--/gNumRealPlayers-preserved/gGameOver-if-<=1/tag-mode ChooseTaggedPlayer logic verbatim. Add ScheduleBecomeBotFromLeave(NSpPlayerLeftMessage*) that maps playerID->playerNum via FindHumanByNSpPlayerID and calls Host_ScheduleFrameEvent(kEvBecomeBot, playerNum).
- `Source/Network/NetHigh.c` (new host event scheduler + per-machine applier) — Host_ScheduleFrameEvent(type,playerNum): effectiveFrame=gHostSendCounter+EV_BECOME_BOT_LEAD; push into sHostPendingEvents ring (host) and RecordFrameEvent into sFrameEventTable (host applies via same code as clients). HostSend (NetHigh.c:794-844) drains up to 2 pending events into gHostOutMess.events[]/eventCount each broadcast until effectiveFrame passes. RecordFrameEvent dedupes into sFrameEventTable on both roles (client from mess->events[]). ApplyPendingFrameEvents(frame) applies entries whose effectiveFrame==frame via ApplyBecomeBot/kEvUnpauseForce, after the per-frame seed exchange and before MoveEverything.
- `Source/Network/NetHigh.c` (Client_InGame_HandleHostControlInfoMessage at NetHigh.c:853-911) — KEEP verbatim; append a loop after the input copy that calls RecordFrameEvent(mess->events[k]) for k in 0..eventCount-1 so the client schedules host-broadcast events (deduped).
- `Source/Network/NetHigh.c` (kNetSequence_WaitingForPlayerVehicles FPS code at NetHigh.c:382-394) — Keep the host's LCD lowering (gTargetFPS=min on each received char-type) but gate it host-only. Remove client-side lowering here (clients adopt the host's finalized value). Leave connectionType/redundancy handling as-is (Stage 2 owns its retirement).
- `Source/Network/NetHigh.c` (HostWaitForPlayersToPrepareLevel at NetHigh.c:689-721) — Set outMess.targetFPS = gTargetFPS (finalized LCD over all char-types incl. host) before the NSpMessage_Send at 717-721. Call Net_MaybeSendKeepAlive() inside the wait loop (689-703) so the host heartbeats while waiting.
- `Source/Network/NetHigh.c` (client sync handlers at NetHigh.c:428-447 and ClientTellHostLevelIsPrepared 767-781) — In kNetSequence_ClientWaitForSyncFromHost on kNetSyncMessage adopt gTargetFPS=((NetSyncMessage*)message)->targetFPS before entering GameLoop. Add Net_MaybeSendKeepAlive() to the ClientTellHostLevelIsPrepared wait loop (767-781).
- `Source/Screens/NetGather.c` (DoNetGatherScreen loop at NetGather.c:193-212) — Call Net_MaybeSendKeepAlive() each lobby iteration (both host and client) so radios stay awake and lastHeard stays fresh while gathering.
- `Source/Screens/Paused.c` (UpdatePausedMenuCallback at Paused.c:142-165) — Replace blocking ClientReceive/ClientSend / HostSend/HostReceive with the Stage 2/3 path: host = Net_Pump(); NetCheck_ConnectionTimeouts(); Host_ConsumeClientInputs(); ApplyPendingFrameEvents(curFrame); HostSend_ControlInfoToClients(). Client = Net_Pump(); NetCheck_ConnectionTimeouts(); consume up to K_max host packets (ApplyPendingFrameEvents after each); SampleAndSendLocalInput(). Full-rate lockstep continues during pause (counters aligned, radios awake).
- `Source/Screens/Paused.c` (TODO at Paused.c:156) — Fix: after the net pump, if gNetSequenceState >= kNetSequence_Error || gGameOver, call CancelMenu('bail') (new Menu.c helper) to break StartMenu and let PlayArea tear down the dead net game.
- `Source/System/Menu.c` (StartMenu at Menu.c:1997 / loop at 2036) — Add public `void CancelMenu(int returnID)` that sets gNav->menuID=returnID and gNav->menuState=kMenuStateFadeOut (guarded like the startButtonExits check, not during AwaitKeyPress/PadPress/MouseClick), letting a menu update-callback exit. Declare in menu.h.
- `Source/System/Main.c` (JNI block at Main.c:1736-1790) — After the MulticastLock, acquire a WifiLock: createWifiLock(WIFI_MODE_FULL_LOW_LATENCY=4,"CroMagRally") then acquire(); keep a GlobalRef. On JNI exception / null (API<29) clear exception and retry mode 3 (FULL_HIGH_PERF). Comment the OS-backgrounding ~8s keepalive-death limitation.
- `Source/Network/NetHigh.c` (EndNetworkGame at NetHigh.c:188-224) — Add/extend ResetNetGameTransientState() (risk 11) called from both Init and End paths to clear gNetBadge[], gLastNetSendMs, sHostPendingEvents, sFrameEventTable, and per-connection lastHeard/hostLastHeard, so a second in-process net game starts clean.


**Data structures.** // --- Frame-aligned events (wire fields live in NetHostControlInfoMessageType, added in the Stage 2 CMR7 bump) ---
enum { kEvNone=0, kEvBecomeBot=1, kEvUnpauseForce=2 };   // reserved values for future events

typedef struct {
    uint32_t  effectiveFrame;   // host frameCounter (gHostSendCounter units) at which every machine applies this
    uint8_t   type;             // kEvBecomeBot, kEvUnpauseForce, ...
    int8_t    playerNum;        // dense internal player index (0..MAX_PLAYERS-1)
    uint16_t  pad;
} NetEvent;                     // 8 bytes, _Static_assert(sizeof(NetEvent)==8)
// In NetHostControlInfoMessageType (Stage 2): uint8_t eventCount; NetEvent events[2];

// --- Host outgoing pending-event ring (re-broadcast until applied) ---
#define NET_MAX_PENDING_EVENTS 8
typedef struct { NetEvent ev; bool active; } PendingEventSlot;
static PendingEventSlot sHostPendingEvents[NET_MAX_PENDING_EVENTS];   // host only

// --- Per-machine apply table (host + clients): dedupe + apply at effectiveFrame ---
typedef struct { uint32_t effectiveFrame; uint8_t type; int8_t playerNum; bool applied; bool valid; } FrameEventEntry;
static FrameEventEntry sFrameEventTable[NET_MAX_PENDING_EVENTS];      // both roles

// --- Connection-liveness policy state ---
static bool      gNetBadge[MAX_PLAYERS];       // host: per-player; client: gNetBadge[0] = host link
static uint32_t  gLastNetSendMs = 0;           // keepalive throttle (bumped by every SendOrEnqueue)

// --- NSpPlayer / NSpGame additions (netsprocket.h) ---
// NSpPlayer:  uint32_t lastHeard;     // host per-client, ms (SDL_GetTicks)
// NSpGame:    uint32_t hostLastHeard; // client tracks host, ms

// --- NetSyncMessage addition (network.h) ---
// uint16_t targetFPS;  uint16_t pad;  // finalized LCD framerate, host -> all in level-prep sync

#define D_MAX 8
#define EV_BECOME_BOT_LEAD (D_MAX + 4)   // ~12 frames (~200ms @60) ahead so all clients receive before applying
#define NET_BADGE_MS    1000
#define NET_DROP_MS    10000
#define NET_KEEPALIVE_MS  50


**Algorithms.** === A. Connection liveness (replaces 4-strike DATA_TIMEOUT) ===
NetCheck_ConnectionTimeouts():            // once/frame each role
  now = SDL_GetTicks()
  if host:
    for each active client slot p (NSpGame.players[]):
      dt = now - players[p].lastHeard
      gNetBadge[playerNum_of(p)] = (dt > NET_BADGE_MS)
      if dt > NET_DROP_MS and not alreadyScheduled(playerNum):
        Host_ScheduleFrameEvent(kEvBecomeBot, playerNum_of(p))   // same path as a clean leave
        // keep socket; host keeps substituting that player until effectiveFrame
  else (client):
    dt = now - gNetGame->hostLastHeard
    gNetBadge[0] = (dt > NET_BADGE_MS)
    if dt > NET_DROP_MS: NetGameFatalError(kNetSequence_ErrorNoResponseFromHost)
  // TCP keepalive (5s+3x1s, already configured) independently surfaces dead peers ~8s as
  // synthesized PlayerLeft/GameTerminated, feeding the same conversion path.

=== B. Keepalive (radios + lastHeard outside in-game streaming) ===
Net_MaybeSendKeepAlive():
  now = SDL_GetTicks()
  if now - gLastNetSendMs < NET_KEEPALIVE_MS: return
  build header-only msg, what=kNetKeepAliveMessage; to = host? kNSpAllPlayers : kNSpHostID
  SendOrEnqueue(...)        // bumps gLastNetSendMs
Receiver: kNetKeepAliveMessage -> no-op; lastHeard already refreshed by NetLow Get path.
Call sites: lobby loop (NetGather), both loading barriers, char-select wait. In-game needs
none (Stage 2/3 stream 60pps both ways by construction).

=== C. Frame-aligned BECOME-BOT (G3 determinism fix) ===
Host learns of leave via synthesized kNSpPlayerLeft (NetLow:802) OR a >10s drop.
Host_ScheduleFrameEvent(type, playerNum):
  if sHostPendingEvents/sFrameEventTable already has unapplied (type,playerNum): return  // dedupe
  effF = gHostSendCounter + EV_BECOME_BOT_LEAD     // gHostSendCounter = frame about to be sent
  push {effF,type,playerNum} into sHostPendingEvents; RecordFrameEvent({effF,...})   // host applies via shared table

In HostSend_ControlInfoToClients (every broadcast):
  eventCount=0
  for slot in sHostPendingEvents where active:
     if slot.ev.effectiveFrame < thisSendFrame: slot.active=false          // expired
     elif eventCount<2: gHostOutMess.events[eventCount++] = slot.ev
  gHostOutMess.eventCount = eventCount
  // Re-sent every frame until effectiveFrame; TCP ordering guarantees each client RECORDS it
  // before consuming the packet whose frameCounter==effectiveFrame.

Client (inside Client_InGame_HandleHostControlInfoMessage, appended):
  for k in 0..mess->eventCount-1: RecordFrameEvent(mess->events[k])         // dedupe by (type,playerNum,effF)

BOTH roles, AFTER the per-frame seed exchange and BEFORE MoveEverything:
ApplyPendingFrameEvents(frameBeingSimulated):
  for e in sFrameEventTable where e.valid and !e.applied and e.effectiveFrame==frameBeingSimulated:
     switch e.type:
        kEvBecomeBot:    ApplyBecomeBot(e.playerNum)
        kEvUnpauseForce: gPlayerInfo[e.playerNum].net.pauseState = 0
     e.applied = true
  // host frameBeingSimulated = frameCounter just stamped in HostSend; client = mess->frameCounter just consumed.
  // ChooseTaggedPlayer()'s RandomRange (synced MyRandomLong) thus draws at the IDENTICAL RNG-stream
  // position on every machine: [seedDraw][event draws][sim draws].

ApplyBecomeBot(playerNum):     // ex-PlayerUnexpectedlyLeavesGame body, keyed by dense index
  if gPlayerInfo[playerNum].isComputer: return    // idempotent (double-leave / drop+leave race)
  gPlayerInfo[playerNum].isComputer = true
  gPlayerInfo[playerNum].isEliminated = true
  gPlayerInfo[playerNum].net.pauseState = 0
  gNumGatheredPlayers--
  if gNumRealPlayers <= 1: gGameOver = true
  if gameMode in {TAG1,TAG2} and gPlayerInfo[playerNum].isIt: ChooseTaggedPlayer()

=== D. gTargetFPS finalize-and-broadcast (fix NetHigh:382-394 asymmetry) ===
During WaitingForPlayerVehicles: HOST ONLY lowers gTargetFPS=min(gTargetFPS,mess.refreshRate) on each
  char-type (host seed from SetupNetworkHosting:475 already folded in). Clients STOP self-lowering.
After GotAllPlayerVehicles: host gTargetFPS = true LCD.
HostWaitForPlayersToPrepareLevel final sync: outMess.targetFPS = gTargetFPS.
Client kNetSequence_ClientWaitForSyncFromHost: gTargetFPS = sync.targetFPS -> GameLoop.
Result: every machine paces at the same rate (host spin-cap Main:1096-1111; clients on vsync+packets).

=== E. Pause callback (Paused.c) ===
UpdatePausedMenuCallback():
  MoveObjects(); DoPlayerTerrainUpdate();
  if gNetGameInProgress:
    if host: Net_Pump(); NetCheck_ConnectionTimeouts(); Host_ConsumeClientInputs();
             ApplyPendingFrameEvents(curFrame); HostSend_ControlInfoToClients();
    else:    Net_Pump(); NetCheck_ConnectionTimeouts();
             for k in 0..K_max-1 while ring nonempty: consume 1 host pkt; ApplyPendingFrameEvents(pkt.frameCounter);
             SampleAndSendLocalInput();
    if gNetSequenceState >= kNetSequence_Error or gGameOver: CancelMenu('bail')

=== F. Android WifiLock (Main.c JNI) ===
After MulticastLock acquire:
  createWifiLock = GetMethodID(wifiManagerClass,"createWifiLock","(ILjava/lang/String;)Landroid/net/wifi/WifiManager$WifiLock;")
  lock = CallObjectMethod(wifiManager, createWifiLock, /*WIFI_MODE_FULL_LOW_LATENCY*/4, NewStringUTF("CroMagRally"))
  if ExceptionCheck or lock==NULL: ExceptionClear; retry mode 3 (FULL_HIGH_PERF)
  CallVoidMethod(lock, GetMethodID(lockClass,"acquire","()V")); globalWifiLock = NewGlobalRef(lock)


**Edge cases:**
- Tagged ('it') player leaves: ChooseTaggedPlayer() must run at effectiveFrame on every machine with identical gNumTotalPlayers and isEliminated state. ApplyBecomeBot sets the leaver isEliminated=true before the ChooseTaggedPlayer draw and all machines do it at the same frameCounter, so the RandomRange draw selects the same new 'it' everywhere — the exact desync G3 targets. Add a gWhoIsIt parity assertion test.
- Leave while paused: gSimulationFrame is frozen but gHostSendCounter keeps advancing (pause menu burns net frames). effectiveFrame is in gHostSendCounter units, so the event still fires. After ApplyBecomeBot forces isComputer=true, IsNetGamePaused() (ignores isComputer) may flip false on all machines at the same frameCounter — deterministic unpause; kEvUnpauseForce is the belt-and-suspenders.
- Double-leave / drop+leave race on the same player: ScheduleFrameEvent dedupes by (type,playerNum); ApplyBecomeBot is idempotent. Two DIFFERENT players leaving same frame: events[2] holds both; >2 simultaneous (up to 4 clients) dribble out 2/broadcast across frames — EV_BECOME_BOT_LEAD (~12 frames) gives slack.
- Leave during loading barrier or lobby (not GameLoop): no running lockstep sim and ChooseTaggedPlayer not yet frame-coupled, so convert immediately (existing path); the frame-aligned path engages only in kNetSequence_GameLoop.
- Host keeps substituting the leaver between TCP-leave arrival and effectiveFrame: Stage 2 substitution fires because the queue is empty; host fills the broadcast slot from held gPlayerInfo[]. Verify the kicked/closed slot is not pruned from the broadcast loop before effectiveFrame.
- Backlogged client still applies at the correct frameCounter (not wall-clock): the event rides every broadcast until effectiveFrame and TCP is ordered, so the client records it before consuming frameCounter==effectiveFrame.
- Keepalive vs in-game streaming: gLastNetSendMs bumped by every SendOrEnqueue means 60pps in-game suppresses keepalives; only idle screens emit them. Don't send a keepalive in the same tick a real packet went out.
- FPS negotiation with a late joiner: config.targetFPS (HostSendGameConfigInfo:613) is now provisional; the authoritative value is the level-prep sync. Clients must ignore config.targetFPS for pacing and wait for the sync value.
- Android createWifiLock(4,...) on API<29 throws/returns null: catch via ExceptionCheck+ExceptionClear, fall back to mode 3; never let a JNI exception reach SDL (Main.c:1791 backstop).
- CancelMenu from the pause callback must not double-free the net game: 'bail' -> gGameOver=true -> PlayArea breaks and runs EndNetworkGame once; ensure no second EndNetworkGame from the GameTerminated path.
- All new transients reset between net games (risk 11): gNetBadge[], gLastNetSendMs, sHostPendingEvents, sFrameEventTable, lastHeard/hostLastHeard cleared in ResetNetGameTransientState() from Init and End paths, or a second in-process game inherits a stale badge/pending-event and wedges.


**Testing.** Use the Stage-0 netns harness (cmr_h/cmr_c1/cmr_c2 + tc netem). SCENARIOS: (1) Tag-mode leave determinism: GAME_MODE_TAG1, c2 holds 'it', kill c2 mid-race; assert all surviving machines log identical gWhoIsIt and zero kNetSequence_SeedDesync for 30s after, on an x86-host + ARM-client pair (catch-up stresses FP). (2) Leave-while-paused: c1 opens pause menu, c2 leaves; assert deterministic unpause and identical sim state on resume. (3) Double-leave same frame: script c1 and c2 sockets to close within one host frame; assert both convert, no fatal, events dribble correctly. (4) Timeout policy: 'tc qdisc change ... loss 100%' on c2 for 1.2s -> badge on host+other client at ~1s, clears on recovery; sustain 11s -> c2 becomes bot at drop (NOT fatal); kill host -> client hits kNetSequence_ErrorNoResponseFromHost at 10s. (5) Keepalive: idle in lobby/char-select 5min on profile D; assert host.lastHeard/hostLastHeard stay <100ms and no >50ms TX gap in pcap. (6) FPS negotiation: host 144Hz + c1 60Hz + c2 120Hz; assert all three end with gTargetFPS==60 (read the F3/OGL_Support:1344 overlay) — the asymmetry bug would leave them disagreeing. (7) Pause-death: kill host while c1 is in the pause menu; assert CancelMenu fires and c1 tears down within one frame. METRICS (CSV/HUD): badge on/off transitions, drop->bot conversions, frame-event apply frame numbers (must match across machines), keepalive count, seed-check pass count. ACCEPTANCE: profile A 20-race bot soak with one scripted leave/race -> zero desync, zero spurious fatals, deterministic tag re-selection every time.


**Risks:**
- HARD DEPENDENCY (risk 9): Paused.c and the loading barriers MUST convert to the pump/sampler path in the same release as Stage 3, or the blocking ClientReceive/HostReceive busy-waits return through the back door. This stage cannot ship on bare master.
- Frame-aligned kEvBecomeBot interacts with pause, race-end, and double-leave (risk 4): needs the full state-machine test matrix; the host must keep substituting between TCP-leave arrival and effectiveFrame or the leaver's car desyncs in the gap.
- effectiveFrame must be in gHostSendCounter (frameCounter) units, NOT gSimulationFrame — the two diverge under pause. Applying at the wrong counter reintroduces the exact ChooseTaggedPlayer draw-count desync this stage fixes.
- gTargetFPS finalize requires NetSyncMessage.targetFPS from the Stage 2 CMR7 bump; if it was not reserved there, adding it now is a wire-ABI change needing a 4CC re-bump and _Static_asserts on all 6 platforms.
- CancelMenu is new Menu.c surface poked from a callback; verify it cannot fire mid-fade or during AwaitKeyPress/PadPress/MouseClick (guard like Menu.c:2045-2047).
- Android WIFI_MODE_FULL_LOW_LATENCY is API 29+; the mode-3 fallback and JNI exception clearing must be exercised on an older device. OS backgrounding still kills the session ~8s (keepalive death) — documented out-of-scope limitation, future 30s pause-grace.
- Stale-state regressions (risk 11): every new transient must be reset via the single ResetNetGameTransientState() from both Init and End, or a second in-process net game inherits a stale badge/pending-event and wedges.
- Substituted/held inputs during the leave grace window are auditable via inputFlags bit0 (Stage 2) but could in principle alter a photo-finish — accepted; hold-last is the least-surprising policy.


## Stage 5 — Cross-platform soak + tuning: x86<->ARM seed-check soak harness, D_i controller tuning, -ffp-contract=off, and the pre-agreed UDP-swap threshold evaluation

**Effort:** 3-5 dev-days. Code is small and mostly mechanical: ~0.5d for the CMake FP flags + per-target build verification across all 6 targets; ~0.5d to hoist Stage-2 constants into gNetTuning + env-override loader; ~0.5d for the seed-desync diagnostics + NetSoak rollup/summary + CLI flags; ~1-2d building/running the netns+netem harness, the x86<->ARM soak, and the D_i tuning sweep; ~0.5d to analyze telemetry and write up the UDP go/no-go verdict. Partially independently shippable: the -ffp-contract=off flag, seed diagnostics, tunable struct, and harness scripts can land and ship on their own as determinism hardening + test infrastructure (valuable even pre-CMR7). The actual soak results, tuning numbers, and UDP decision require Stages 1-4 merged AND a real ARM device, so the *validation* portion is gated on the rest of the redesign and on hardware availability, not on this stage's code.

**Objective.** Stage 5 is the validation-and-tuning gate that proves the CMR7 rev-B netcode (built in Stages 0-4) is bit-deterministic across architectures and well-tuned on hostile links, then renders the pre-agreed UDP-contingency go/no-go decision. It contributes only four small, surgical code changes plus a reusable test harness: (1) pin floating-point contraction off across all 6 targets so x86 SSE2 and ARM64 FMA cannot disagree on the number of synced-RNG draws (the per-frame fatal seed check at NetHigh.c:876 guards draw COUNT, not bit-exact position); (2) hoist the Stage-2 adaptive-depth (D_i) controller's magic numbers into one runtime-tunable struct so soak iterations don't require recompiles, then lock in tuned defaults validated against netem profile D; (3) extend the seed-desync fatal path with simTick + per-client ackInputSeq diagnostics and a telemetry record so any cross-arch divergence is pinpointed to a reproducible frame; (4) add deterministic-seed / multi-race / all-bots / A-B CLI flags so an x86-host + ARM-client soak can be scripted to assert zero kNetSequence_SeedDesync and zero spurious fatals. On top of the Stage-0 CSV telemetry, a per-race summary aggregator computes the named UDP exit-criterion (>2 multi-frame substitution events/min on a healthy 5GHz link) and emits a machine-readable verdict. No wire bytes change (all CMR7 fields landed in the Stage-2 bump); the harness drives the existing --host/--join/--port CLI + gAutoPilot bot autopilot.


**Changes:**

- `CMakeLists.txt` (target_compile_options(${GAME_TARGET} ...) non-MSVC block, CMakeLists.txt:185-197) — Add deterministic-FP flags to the GCC/Clang branch (covers Linux, macOS arm64+x86_64, Android, iOS, tvOS): -ffp-contract=off (kill FMA contraction so (a*b)+c is identical on ARM64 and x86 SSE2) and -fno-fast-math (defensive; ensure no upstream toolchain default enables fast-math for the sim target). Place AFTER -Wstrict-aliasing=2. Do NOT add -ffast-math/-funsafe-math-optimizations. Leave extern/gl4es's -ffast-math (gl4es CMakeLists.txt:56-127) untouched — that is the GL emulation layer, not the deterministic sim.
- `CMakeLists.txt` (target_compile_options(${GAME_TARGET} ...) MSVC else-branch, CMakeLists.txt:217-229) — Add /fp:precise to the MSVC option list (Windows x64 + ARM64). /fp:precise does not contract FMA by default; do not pass /fp:fast. This matches the non-MSVC -ffp-contract=off so the Windows targets agree with the others on synced-RNG draw count.
- `Source/Headers/file.h` (struct CommandLineOptions, file.h:113-123) — Add soak/determinism fields: int netPort; const char* netJoinAddr; uint32_t soakSeed; int soakRaces; bool soakBots; bool netSendPerPacket; const char* telemetryCsv;  (netPort/netJoinAddr restore the --port/--join<addr> plumbing the harness needs — see risk on task #11 dependency).
- `Source/Boot.cpp` (ParseCommandLine(), Boot.cpp:90-129) — Parse the new flags: --port <n> -> gCommandLine.netPort (also assign gNetPort, NetLow.c:36); --join <addr> -> gCommandLine.netJoin=true + netJoinAddr; --host -> netHost; --seed <hex|dec> -> soakSeed; --soak <n> -> soakRaces; --bots -> soakBots; --net-ab -> netSendPerPacket (reverts the Stage-2 wall-clock sender to send-per-consumed-packet for A/B isolation); --telemetry <path> -> telemetryCsv. Keep existing GAME_ASSERT_MESSAGE arg-count guards. NOTE: confirm --host/--join/--port are actually wired here (the synthesis assumes them 'verified working' but ParseCommandLine currently lacks them — they are a Stage-0/task-#11 hard prerequisite for the harness).
- `Source/System/Misc.c` (InitMyRandomSeed(), Misc.c:231-234 and SetMyRandomSeed(), Misc.c:224-227) — If gCommandLine.soakSeed != 0, seed gSimRNG from it via InitSimRNG(soakSeed) instead of the hardcoded 0x2a80ce30, and (only in soak mode) seed gLocalRNG from a fixed value too so an all-bots race is fully reproducible. Gate behind soak mode so normal play is unaffected. This makes the diverging frame in a cross-arch desync reproducible run-to-run.
- `Source/Network/NetTuning.h / NetTuning.c` (NEW files (glob-picked by CMakeLists.txt:147 file(GLOB_RECURSE))) — Define NetTuning gNetTuning with the Stage-2 controller constants as fields (see dataStructures). Defaults in NetTuning.c are the post-tuning locked values. Add LoadNetTuningOverrides() that reads CMR_NET_* environment variables (and, if present, a key=value file path from --net-tune) so a soak sweep can vary one parameter per process without rebuilding. Stage 2's #define constants in NetHigh.c are replaced by gNetTuning.<field> references.
- `Source/Network/NetHigh.c` (Stage-2 Host_ConsumeClientInputs depth-controller + substitution/coalesce body (the new function added in Stage 2)) — Replace the inline #defines (jitter window 128, P95, D clamp 1..8, D_init 2/6, 2s decay, grace re-poll 2x@1ms, 30-frame neutral decay, K_max=3) with gNetTuning.<field>. Behavior identical at default values; this only makes them tunable. If gCommandLine.netSendPerPacket is set, the client wall-clock sampler (Stage-2 SampleAndSendLocalInput) falls back to one send per consumed packet (build-flag A/B path required by risk #3).
- `Source/Network/NetHigh.c` (Client_InGame_HandleHostControlInfoMessage seed check, NetHigh.c:876-880) — Before calling NetGameFatalError(kNetSequence_SeedDesync), capture a diagnostic record: localGotSeed=MyRandomLong() result, expectedSeed=mess->randomSeed, mess->frameCounter, mess->simTick, local gSimulationFrame, and the per-client mess->ackInputSeq[] (the Stage-2 ack field). Call NetSoak_RecordDesync(...) (writes the CSV row + sets gSoakRace.seedDesyncCount) so the soak harness pinpoints the exact frame/seq where x86 and ARM diverged. The seed check itself stays fatal and unchanged in semantics.
- `Source/Network/NetHigh.c` (NetGameFatalError(), NetHigh.c:83-92) — In the non-_DEBUG branch, before EndNetworkGame(), call NetSoak_RecordFatal(error) so any non-seed fatal (lost packet, no-response) is counted in the soak summary and surfaced in the harness exit code. In _DEBUG, augment the DoFatalAlert format string with simTick + gSimulationFrame for on-device triage.
- `Source/Network/NetSoak.h / NetSoak.c` (NEW files (glob-picked)) — Per-race telemetry rollup (NetSoakRace gSoakRace, see dataStructures) fed by the Stage-0 CSV logger callbacks (substitution, coalesce, hold-last, delivery-gap, RTO-shaped-gap, ack-delay events). NetSoak_BeginRace()/NetSoak_EndRace() bracket each PlayArea; NetSoak_EndRace() computes the UDP exit-criterion (multiFrameSubstEvents per minute and rtoShapedGaps per minute per client), frame-time p50/p95/p99 from the histogram, own-input-delay p95 from ack samples, prints a one-line VERDICT, appends a summary row to telemetryCsv, and (in --soak mode) accumulates pass/fail. A non-zero process exit code is returned if seedDesyncCount>0 or spuriousFatalCount>0.
- `Source/System/Main.c` (PlayArea() race entry/exit (around the level-prep barrier Main.c:969-983 and CleanupLevel/EndNetworkGame at the bottom) and gAutoPilot toggles (Main.c:339/371)) — When gCommandLine.soakRaces>0: bracket the race with NetSoak_BeginRace()/NetSoak_EndRace(); force gAutoPilot=true and mark all human slots isComputer when --bots so a race needs no input device; after EndNetworkGame, if more soak races remain, loop back into a fresh hosted/joined race (re-validates the second-game-in-process reset path that Stage-0 fixed). On the final race, exit the process with NetSoak's accumulated code so a shell soak loop can assert success.
- `tools/soak/run_soak.sh + tools/soak/profiles.sh` (NEW harness scripts (not compiled; test artifacts)) — Bring up 3 network namespaces (cmr_h/cmr_c1/cmr_c2) + veth pairs to a bridge; apply per-veth tc qdisc netem profiles A-E from profiles.sh; launch one game build per namespace via --host / --join <hostaddr> --port + --bots --seed <fixed> --soak <N> --telemetry <ns>.csv; for the cross-arch run, launch the ARM build (Android via adb/scrcpy-less headless, or Apple Silicon / ARM64 Linux) as a client against the x86 host. Collect CSVs, run tools/soak/summarize.py to assert acceptance and print the UDP-swap verdict.


**Data structures.** /* ---- Source/Network/NetTuning.h : runtime-tunable controller params (hoisted from Stage-2 #defines) ---- */
typedef struct
{
    int   jitterWindow;          /* inter-arrival samples for percentile (default 128)               */
    float jitterPercentile;      /* 0.95f = P95 deviation feeding D_i                                 */
    int   dMin, dMax;            /* clamp on per-client depth D_i (default 1, 8)                      */
    int   dInitWired;            /* D seed when connectionType==0 (default 2)                         */
    int   dInitWifi;             /* D seed when connectionType==1 (default 6)                         */
    float decaySeconds;          /* wall-seconds per 1-frame depth decay (default 2.0f)               */
    float decayP99MarginFrames;  /* require >= this P99 margin before decaying (default 1.0f)         */
    int   graceRepollRetries;    /* B_i==0 re-poll attempts before substitute (default 2)             */
    int   graceRepollDelayMs;    /* SDL_Delay between re-polls (default 1)                            */
    int   substNeutralAfter;     /* substituted frames before throttle/steer decay to neutral (30)    */
    int   clientCatchupKMax;     /* client bounded catch-up packets / render frame (default 3)        */
    float dtMaxClampMul;         /* net-only dt clamp = mul / gTargetFPS (default 2.0f ~= 33ms)       */
    int   sendRingBytes;         /* per-socket send ring (default 32768)                              */
    int   hostInputQueueSlots;   /* per-client input ring (default 128)                               */
    int   clientPacketRingSlots; /* host-packet ring (default 32)                                     */
} NetTuning;
extern NetTuning gNetTuning;     /* defaults in NetTuning.c; LoadNetTuningOverrides() applies CMR_NET_* env vars */

/* ---- Source/Network/NetSoak.h : per-race telemetry rollup + UDP exit-criterion ---- */
typedef struct
{
    uint32_t seedDesyncCount;                    /* fatal kNetSequence_SeedDesync (MUST be 0)         */
    uint32_t spuriousFatalCount;                 /* any other NetGameFatalError                       */
    uint32_t simFramesTotal;                     /* gSimulationFrame span of the race                 */
    double   wallSecondsTotal;                   /* race wall-clock duration                          */
    /* UDP-swap exit-criterion inputs, per client (MAX_CLIENTS=4) */
    uint32_t multiFrameSubstEvents[MAX_CLIENTS]; /* runs of >1 consecutive substituted host frames    */
    uint32_t rtoShapedGaps[MAX_CLIENTS];         /* uplink delivery gaps > 150 ms                     */
    uint32_t substFramesTotal[MAX_CLIENTS];
    uint32_t coalesceEvents[MAX_CLIENTS];
    uint32_t maxDeliveryGapMs[MAX_CLIENTS];
    uint8_t  dMaxObserved[MAX_CLIENTS];          /* peak D_i reached                                  */
    uint32_t ackDelayFramesSamples[MAX_CLIENTS]; /* count + running sum for p95 own-input delay       */
    uint64_t ackDelayFramesSum[MAX_CLIENTS];
    uint32_t holdLastEvents;                     /* client hold-last-frame occurrences                */
    uint32_t frameTimeHistMs[64];                /* render frame-time histogram for p50/p95/p99       */
} NetSoakRace;
extern NetSoakRace gSoakRace;

/* Diagnostic record captured at the seed-desync fatal (NetHigh.c:876) */
typedef struct
{
    uint32_t frameCounter;       /* mess->frameCounter                                                */
    uint32_t hostSimTick;        /* mess->simTick                                                     */
    uint32_t localSimFrame;      /* gSimulationFrame                                                  */
    uint32_t expectedSeed;       /* mess->randomSeed                                                  */
    uint32_t localGotSeed;       /* MyRandomLong() return that mismatched                             */
    uint32_t ackInputSeq[MAX_CLIENTS]; /* mess->ackInputSeq[] : last REAL input seq applied per client */
} NetSoakDesyncRecord;

/* ---- Source/Headers/file.h : CommandLineOptions additions ---- */
/* int netPort; const char* netJoinAddr; uint32_t soakSeed;
   int soakRaces; bool soakBots; bool netSendPerPacket; const char* telemetryCsv; */


**Algorithms.** /* ===== A. UDP-swap exit-criterion (NetSoak_EndRace) — renders the pre-agreed go/no-go ===== */
NetSoak_EndRace():
  mins = max(gSoakRace.wallSecondsTotal / 60.0, 1e-6)
  udpRecommended = false
  for c in activeClients:
     mfsPerMin = gSoakRace.multiFrameSubstEvents[c] / mins
     rtoPerMin = gSoakRace.rtoShapedGaps[c]        / mins
     ackP95Frames = percentile95(ack samples for c)   # own-input delay in frames
     ownDelayMs   = ackP95Frames * (1000/gTargetFPS) + halfRTT_ms[c]
     # PRE-AGREED CRITERION (synthesis risks #1): healthy-link trigger
     if linkIsHealthy(c) and mfsPerMin > 2.0:          # >2 multi-frame subst events/min on 5GHz-class
        udpRecommended = true
  p50,p95,p99 = histPercentiles(gSoakRace.frameTimeHistMs)
  verdict = (gSoakRace.seedDesyncCount==0 && gSoakRace.spuriousFatalCount==0) ? "PASS" : "FAIL"
  printf("SOAK %s seedDesync=%u fatals=%u frameMs p50=%.1f p95=%.1f p99=%.1f "
         "UDP_CONTINGENCY=%s\n", verdict, ...)
  append summary row to telemetryCsv
  process exit code = (verdict=="PASS") ? 0 : 1   # only on final --soak race
  # linkIsHealthy(c): mean inter-arrival jitter under a 5GHz-class bound AND loss<0.5% from netem profile tag

/* ===== B. Cross-arch seed-desync capture (at NetHigh.c:876) ===== */
got = MyRandomLong()
if got != mess->randomSeed:
    rec = { mess->frameCounter, mess->simTick, gSimulationFrame,
            mess->randomSeed, got, copy(mess->ackInputSeq[]) }
    NetSoak_RecordDesync(&rec)           # CSV row + gSoakRace.seedDesyncCount++
    NetGameFatalError(kNetSequence_SeedDesync)   # unchanged: still fatal
    return false
# Because clients only ever simulate the host echo (Main.c:1075-1083 overwritten by
# NetHigh.c:882-887), a mismatch means the SIM RNG draw COUNT diverged between x86 and ARM
# — i.e. a float comparison that gates a synced draw flipped. The record names the frame.

/* ===== C. -ffp-contract=off validation oracle (offline, before tuning) ===== */
# 1. Build x86_64 and ARM64 (Apple-Silicon or ARM64-Linux/Android) WITH the new flags.
# 2. Run profile A (clean, 0ms) x86-host + ARM-client, --bots --seed S --soak 20.
# 3. PASS iff every race ends with seedDesync=0. A surviving desync means a residual
#    cross-libm divergence (sinf/cosf of speed, Player_Car chaos) that -ffp-contract=off
#    does NOT fix — escalate to Stage-0 follow-up: route that specific draw through
#    ChaoticFloat (Misc.c:243-253, integer-hash, platform-stable) or a synced-RNG draw.
# 4. Re-run WITHOUT the flags to confirm the flag is load-bearing (expect desyncs return).

/* ===== D. D_i controller tuning sweep (against profile D, no recompiles via gNetTuning env) ===== */
sweep = cartesian({
   CMR_NET_JITTER_PCTL: {0.90, 0.95, 0.99},
   CMR_NET_DECAY_SECONDS: {1.0, 2.0, 4.0},
   CMR_NET_DINIT_WIFI: {4, 6},
   CMR_NET_GRACE_RETRIES: {0, 1, 2},
})
for cfg in sweep:
   export cfg as CMR_NET_* ; run 10-race profile-D soak (x86 host, ARM client)
   record: ownDelayP95ms, substPerMin, multiFrameSubstPerMin, seedDesync, frameMs.p99
keep = { cfg : seedDesync==0 AND multiFrameSubstPerMin<=2 }   # hard constraints
pick = argmin over keep of ownDelayP95ms, tie-break lower frameMs.p99
write pick into NetTuning.c defaults (gNetTuning) and remove env dependence for ship
# Rationale: minimize own-input latency (measured live via ack field) subject to the same
# multi-frame-substitution bound that triggers the UDP contingency, so tuning and the
# UDP decision use one consistent metric.

/* ===== E. Harness loop (tools/soak/run_soak.sh) ===== */
for ns in (cmr_h, cmr_c1, cmr_c2): ip netns add ns; veth ns<->bridge
for veth,profile in mapping: tc qdisc add dev <veth> root netem <profile>
ip netns exec cmr_h  ./CroMagRally --host  --bots --seed S --soak N --port P --telemetry h.csv &
ip netns exec cmr_c1 ./CroMagRally --join <hIP> --bots --seed S --soak N --port P --telemetry c1.csv &
ip netns exec cmr_c2 ARMBUILD --join <hIP> --bots --seed S --soak N --port P --telemetry c2.csv &
wait; python3 tools/soak/summarize.py *.csv   # asserts acceptance + prints UDP verdict
# profile E (acceptance): c1=clean, c2=profile D ; assert wired-client p99<=1.2F & ownDelay<=40ms


**Edge cases:**
- -ffp-contract=off reduces but does NOT eliminate cross-arch divergence: sinf/cosf and other libm transcendentals still differ bit-for-bit between bionic (Android), glibc (Linux), Apple libm, and MSVCRT. The seed check (NetHigh.c:876) only guards synced-RNG DRAW COUNT — position drift is absorbed by the 0.2 rubber-band. A surviving soak desync means a libm result flipped a float branch that gated a synced draw; fix that specific site (route through integer ChaoticFloat or fold the condition into a synced-RNG draw), do not chase bit-exact transcendentals globally.
- ChaoticFloat (Misc.c:243-253) does seedVal*10.0f then casts to uint32 and hashes with integer ops — stable across platforms; the Player_Car.c:2021-2027 collision-spin path is already platform-safe and must stay on ChaoticFloat, not be 'fixed' to a synced draw.
- Soak --seed override must be gated to soak mode only (Misc.c:231-234): forcing a fixed gLocalRNG in normal play would make 'visual' randomness identical every session.
- gl4es is built with -ffast-math (gl4es CMakeLists.txt:56-127); leave it — it is the GL emulation layer, never the sim. Only the game target gets -ffp-contract=off. Pomme (Microseconds/TickCount) carries no sim float math, so its flags don't matter for determinism.
- Second-game-in-process: the --soak N loop re-enters a fresh race; it re-exercises the Stage-0 EndNetworkGame reset (sHostInputQueues, gTimeoutCounter, gClientSendCounter, ClientSend history). If a desync only appears on race 2+, suspect a missed reset, not FP — the ResetNetGameTransientState() from Stage-0/risk-11 must cover gNetTuning-adjacent statics too.
- All-bots determinism: gAutoPilot (Main.c:339/371, Player_Car.c:513) drives CPU steering; if any AI path reads VisualRandomLong (unsynced) the bot trajectories diverge harmlessly (positions rubber-band) but must NOT change synced-draw count — verify the Stage-0 RNG-hygiene set already covers AI-reachable draws or the soak will false-positive a desync.
- Harness depends on --host/--join <addr>/--port actually parsing in Boot.cpp:90-129 (currently absent; pending task #11). Without them the netns harness cannot target the host IP — hard prerequisite, surface in the orchestrator.
- Wall-clock sender A/B (--net-ab / netSendPerPacket): if a soak shows host-side input gaps, toggling this isolates whether the Stage-2 sampler (risk #3) is the cause; the revert path must keep counters aligned or it will itself desync.
- netem 'distribution pareto' and scripted 'tc qdisc change' bursts (profiles C/D) require the sch_netem kernel module and CAP_NET_ADMIN in the namespace; document the modprobe/sysctl prerequisites so CI doesn't silently run profile A under a no-op qdisc.
- ARM client over real WiFi (not netem) for the device-soak leg: OS backgrounding kills the session in ~8s (keepalive death, documented limitation) — keep the device foregrounded/awake during multi-race soaks or races will end on kNetSequence_ErrorNoResponseFromHost and be miscounted as fatals.
- Frame-time histogram bucketing: a host GC/OS hiccup produces one dt-clamped (2/gTargetFPS) frame on EVERY machine by construction — that is expected lockstep behavior, not a client stall; the summarizer must attribute >50ms stalls to the link that caused them (per-client gap data), not to the host clamp.
- UDP verdict must only fire on a HEALTHY link: a profile-D (congested 2.4GHz) run is EXPECTED to exceed 2 multi-frame-subst events/min and must NOT trigger the contingency — linkIsHealthy() gates on the netem profile tag / measured jitter+loss, otherwise the criterion is meaningless.


**Testing.** VALIDATION IS THE STAGE. Build x86_64 (host) and ARM64 (client: Apple Silicon, ARM64 Linux, or Android) with the new flags. Run the tools/soak harness over the synthesis profiles, all bot-driven (--bots, gAutoPilot fully exercises the lockstep wire), fixed --seed for reproducibility. (1) FP/determinism oracle: profile A, x86-host + ARM-client, --soak 20 -> assert zero kNetSequence_SeedDesync and zero spurious fatals; re-run without -ffp-contract=off to confirm the flag is load-bearing (expect desyncs to reappear). (2) D_i tuning: profile D, run the algorithm-D env sweep, pick the config minimizing ack-measured own-input-delay p95 subject to multiFrameSubst<=2/min and zero desync, then bake into gNetTuning defaults and re-verify. (3) Acceptance profile E (c1 clean, c2 profile D): assert wired-client frame-time p99 <= 1.2/gTargetFPS and own-input delay <= 40ms while c2 runs profile D, and no global stall >50ms attributable to c2. (4) Profile C (spiky 5GHz): <=1 visible hitch (>100ms hold) per 10 min. (5) UDP exit-criterion: NetSoak_EndRace prints 'UDP_CONTINGENCY=true/false'; on a healthy 5GHz-class run (profile B) it must read false (else the design's TCP-RTO residual is worse than modeled and the named NetLow.c UDP+K=10 swap is triggered). Metrics come from the Stage-0 CSV logger + NetSoakRace rollup: per-client D_i, queue depth, substitutions/min, multi-frame substitution events, coalesce events, max delivery gap, RTO-shaped gaps (>150ms), send-ring high-water, hold/extrap events, ack input delay (ownSeq-ack)*F, frame-time p50/p95/p99, seed-check pass count. CI gate: process exit code 0 from the final --soak race == pass; summarize.py asserts the acceptance thresholds and fails the job otherwise.


**Risks:**
- see risks field


## Stage 6 — Visual extrapolation done right — transform-rebuilt car/wheel/head dead-reckoning on held client frames (optional polish, behind a flag)

**Effort:** 3-4 dev-days. Independently shippable as optional polish behind gEnableVisualExtrapolation, but ONLY after Stage 3 (hard dependency on the hold-last hook + StepGameSimulation factoring). No wire-format change, no new threads, no engine-core change — one new ~200-line file plus three small Main.c hooks and a header decl. Most of the time is the determinism soak + A/B playtest validation, not the code.

**Objective.** Replace Stage 3's "hold-last-frame" (re-render the identical previous frame when the client's host-packet ring is empty) with short, bounded visual extrapolation so a downlink gap shows continued apparent motion instead of a frozen image. On a held client frame, advance every car's Coord/Rot.y by its own engine velocity (Delta units/s, DeltaRot.y rad/s) × elapsed-since-last-real-step (capped 100 ms), rebuild the car body's BaseTransformMatrix AND re-anchor its chained wheel/head (and shadow) matrices, render, then restore every touched field bit-for-bit. The pass is a PURE render-time transform that touches no simulation state (no gSimulationFrame, no synced RNG draw) and is fully reverted before any sim step, so it cannot perturb lockstep — determinism is preserved by construction. Camera is intentionally frozen (it follows from not calling UpdateCameras on a held frame; OGL re-uses the last cameraPlacement), and own-car extrapolation defaults OFF to avoid the local car sliding under a static camera. Ship only if Stage 5 playtests show hold-last feels insufficient; gate behind gEnableVisualExtrapolation. Hard dependency: Stage 3 must have landed (the empty-ring hold-last branch, StepGameSimulation() factoring, and a per-frame "held this frame" signal).


**Changes:**

- `Source/Network/NetSmooth.c` (NEW FILE (auto-picked by CMakeLists.txt:147 file(GLOB_RECURSE ... CONFIGURE_DEPENDS); just re-run cmake)) — Implements the whole stage: NetSmooth_RecordRealStep(), NetSmooth_BeginExtrapolation(), NetSmooth_EndExtrapolation(); the per-car snapshot ring; the Delta/DeltaRot dead-reckoning; the delta-matrix re-anchoring of wheel/head/shadow nodes. Includes <SDL.h> for SDL_GetPerformanceCounter/Frequency, and the engine headers for ObjNode/OGLMatrix4x4/UpdateObjectTransforms/SetObjectTransformMatrix/UpdateShadow/OGLMatrix4x4_Multiply/OGLMatrix4x4_Invert.
- `Source/Headers/network.h` (after the Client_* prototypes block (alongside the Stage 3 client decls)) — Add prototypes: void NetSmooth_RecordRealStep(void); Boolean NetSmooth_BeginExtrapolation(void); void NetSmooth_EndExtrapolation(void); and extern Boolean gEnableVisualExtrapolation, gExtrapolateLocalCar.
- `Source/System/Main.c` (globals near Main.c:26-27 (gTargetFPS/gUseRedundancy)) — Define Boolean gEnableVisualExtrapolation = false; (default off until Stage 5 validates) and Boolean gExtrapolateLocalCar = false; (freeze own car under the frozen camera). Wire to a pref/CLI flag if desired.
- `Source/System/Main.c` (StepGameSimulation() (Stage 3 refactor of the sim block at Main.c:1052-1069), right after the real MoveEverything()+gSimulationFrame++ at Main.c:1064-1068) — Call NetSmooth_RecordRealStep(); once per REAL simulated frame so the extrapolator knows the wall-clock timestamp of the last authoritative step.
- `Source/System/Main.c` (render call OGL_DrawScene(DrawTerrain); at Main.c:1090) — On a held client frame (Stage 3's gClientHeldThisFrame / empty-ring path), wrap the render: Boolean ex = (gIsNetworkClient && gClientHeldThisFrame && !IsNetGamePaused()) ? NetSmooth_BeginExtrapolation() : false; OGL_DrawScene(DrawTerrain); if (ex) NetSmooth_EndExtrapolation();  Non-held frames render exactly as today.
- `Source/System/Main.c` (Stage 3 client consume loop region (replaces ClientReceive_ControlInfoFromHost at Main.c:1036-1041)) — Confirm/define the Boolean gClientHeldThisFrame signal set true when the K_max consume loop consumed zero real host packets (ring empty at k==0). This is the Stage 3 hold-last predicate that Stage 6 hooks; if Stage 3 named it differently, alias to that.


**Data structures.** // ---- Source/Network/NetSmooth.c ----
#define NETSMOOTH_MAX_CHILDREN   5      // 4 wheels + 1 head (car saved separately)
#define NETSMOOTH_CAP_SECONDS    0.100f // extrapolate at most 100 ms, then hold

// One node whose BaseTransformMatrix we overwrote and must restore.
typedef struct {
    ObjNode*     node;            // nil = unused slot
    OGLMatrix4x4 savedMatrix;     // BaseTransformMatrix before extrapolation
} NetSmoothNodeSave;

// Per-car save record (car body + its children + its shadow).
typedef struct {
    ObjNode*          car;                 // gPlayerInfo[i].objNode; nil => slot not extrapolated
    OGLPoint3D        savedCarCoord;       // car->Coord  before advance
    float             savedCarRotY;        // car->Rot.y  before advance
    NetSmoothNodeSave carSave;             // car body matrix
    int               numChildren;         // 0..NETSMOOTH_MAX_CHILDREN
    NetSmoothNodeSave child[NETSMOOTH_MAX_CHILDREN];
    // shadow (optional)
    ObjNode*          shadow;              // car->ShadowNode (nil if none)
    OGLPoint3D        savedShadowCoord;
    float             savedShadowRotY;
    float             savedShadowScaleX, savedShadowScaleZ;
    OGLMatrix4x4      savedShadowMatrix;
} NetSmoothCarSave;

static NetSmoothCarSave sSave[MAX_PLAYERS];     // MAX_PLAYERS == 6
static Boolean          sExtrapActive = false;  // re-entrancy guard (Begin/End must pair)
static uint64_t         sLastRealStepTick = 0;  // SDL_GetPerformanceCounter() of last real sim step

// Config (defined in Main.c, extern here)
Boolean gEnableVisualExtrapolation = false;
Boolean gExtrapolateLocalCar       = false;


**Algorithms.** // === NetSmooth_RecordRealStep() — called once per REAL simulated frame ===
sLastRealStepTick = SDL_GetPerformanceCounter();

// === Matrix re-anchoring identity (VERIFIED against this codebase) ===
// Engine convention: OGLMatrix4x4_Multiply(A,B,R) computes R = B·A
//   (proven: Objects.c:1272 builds Base = Multiply(scale, rotTrans) = rotTrans·scale,
//    and Player_Car.c:3446 builds wheel = Multiply(local, carMatrix) = carMatrix·local).
// So a child's world matrix W_old = oldCar · L (L = wheel offset/spin/steer, constant during a held frame).
// We want W_new = newCar · L.  Since L = oldCar^-1 · W_old:
//   W_new = newCar · oldCar^-1 · W_old = (newCar·oldCar^-1) · W_old.
// In engine calls:
//   OGLMatrix4x4_Invert(&oldCar, &inv);                 // inv = oldCar^-1
//   OGLMatrix4x4_Multiply(&inv, &newCar, &deltaM);      // deltaM = newCar·oldCar^-1
//   OGLMatrix4x4_Multiply(&child, &deltaM, &child);     // child = deltaM·child = W_new
// This needs NO wheel-offset tables and re-runs NO gameplay (no wheel spin, no debris/RNG).

// === Boolean NetSmooth_BeginExtrapolation() ===
if (!gEnableVisualExtrapolation || !gIsNetworkClient) return false;
freq      = SDL_GetPerformanceFrequency();
extrapSec = (double)(SDL_GetPerformanceCounter() - sLastRealStepTick) / (double)freq;
if (extrapSec <= 0.0) return false;
if (extrapSec > NETSMOOTH_CAP_SECONDS) extrapSec = NETSMOOTH_CAP_SECONDS;  // cap → image holds past 100ms
GAME_ASSERT(!sExtrapActive);
sExtrapActive = true;

for (i = 0; i < gNumTotalPlayers && i < MAX_PLAYERS; i++) {
    s = &sSave[i]; s->car = nil; s->shadow = nil;
    car = gPlayerInfo[i].objNode;
    if (!car || car->CType == INVALID_NODE_FLAG) continue;
    if (car->StatusBits & (STATUS_BIT_HIDDEN|STATUS_BIT_NOSHOWTHISPLAYER)) continue; // 1st-person/hidden
    if (!gExtrapolateLocalCar && i == gMyNetworkPlayerNum) continue;                  // freeze own car w/ camera

    // walk chain exactly like AlignWheelsAndHeadOnCar (Player_Car.c:3340-3358); SKIP (don't fatal) if broken
    w0=car->ChainNode; w1=w0?w0->ChainNode:0; w2=w1?w1->ChainNode:0;
    w3=w2?w2->ChainNode:0; head=w3?w3->ChainNode:0;
    if (!w0||!w1||!w2||!w3||!head) continue;     // mid-teardown -> skip this car

    // SAVE scalars + matrices
    s->car=car; s->savedCarCoord=car->Coord; s->savedCarRotY=car->Rot.y;
    s->carSave.node=car; s->carSave.savedMatrix=car->BaseTransformMatrix;
    children[5]={w0,w1,w2,w3,head}; s->numChildren=5;
    for(c=0;c<5;c++){ s->child[c].node=children[c]; s->child[c].savedMatrix=children[c]->BaseTransformMatrix; }
    s->shadow=car->ShadowNode;
    if (s->shadow){ s->savedShadowCoord=s->shadow->Coord; s->savedShadowRotY=s->shadow->Rot.y;
        s->savedShadowScaleX=s->shadow->Scale.x; s->savedShadowScaleZ=s->shadow->Scale.z;
        s->savedShadowMatrix=s->shadow->BaseTransformMatrix; }

    // ADVANCE car by its own engine velocity (mirrors Player_Car.c:880-882 and 1610)
    oldCar = car->BaseTransformMatrix;
    car->Coord.x += car->Delta.x * extrapSec;
    car->Coord.y += car->Delta.y * extrapSec;
    car->Coord.z += car->Delta.z * extrapSec;
    car->Rot.y   += car->DeltaRot.y * extrapSec;
    UpdateObjectTransforms(car);                 // Objects.c:1233 rebuilds car Base + mirror from new Coord/Rot.y
    newCar = car->BaseTransformMatrix;

    // RE-ANCHOR children rigidly (no gameplay re-run)
    OGLMatrix4x4_Invert(&oldCar,&inv); OGLMatrix4x4_Multiply(&inv,&newCar,&deltaM);
    for(c=0;c<5;c++){ OGLMatrix4x4_Multiply(&children[c]->BaseTransformMatrix,&deltaM,&children[c]->BaseTransformMatrix);
                      SetObjectTransformMatrix(children[c]); }      // Objects.c:1285 refresh BaseTransformObject mirror

    // SHADOW re-anchor (terrain-conforming) using the now-extrapolated car->Coord
    if (s->shadow) UpdateShadow(car);            // Objects2.c:404 repositions+rescales shadow at new coord
}
return true;

// === void NetSmooth_EndExtrapolation() — restore EVERYTHING bit-for-bit ===
if (!sExtrapActive) return;
for (i = 0; i < gNumTotalPlayers && i < MAX_PLAYERS; i++) {
    s=&sSave[i]; if(!s->car) continue;
    s->car->Coord = s->savedCarCoord;  s->car->Rot.y = s->savedCarRotY;
    s->car->BaseTransformMatrix = s->carSave.savedMatrix; SetObjectTransformMatrix(s->car);
    for(c=0;c<s->numChildren;c++){ s->child[c].node->BaseTransformMatrix=s->child[c].savedMatrix;
                                   SetObjectTransformMatrix(s->child[c].node); }
    if (s->shadow){ s->shadow->Coord=s->savedShadowCoord; s->shadow->Rot.y=s->savedShadowRotY;
        s->shadow->Scale.x=s->savedShadowScaleX; s->shadow->Scale.z=s->savedShadowScaleZ;
        s->shadow->BaseTransformMatrix=s->savedShadowMatrix; SetObjectTransformMatrix(s->shadow); }
    s->car=nil; s->shadow=nil;
}
sExtrapActive=false;


**Edge cases:**
- extrapSec <= 0 (a real packet was consumed this frame): Begin returns false, render proceeds normally — never happens on a held frame but cheaply guarded.
- Gap > 100 ms: extrapSec clamps to NETSMOOTH_CAP_SECONDS, so the picture freezes at the 100 ms-extrapolated pose (matches synthesis '100ms extrapolation + 100ms hold'); Stage 3's net badge still appears at >=250 ms.
- Net pause (IsNetGamePaused()): caller guards extrapolation OFF — cars must not coast while paused even if Delta!=0.
- Own car: gExtrapolateLocalCar defaults false → local car frozen together with the (frozen) camera, so there is no own-car-slide artifact; remote cars still glide.
- 1st-person / hidden car (STATUS_BIT_NOSHOWTHISPLAYER|HIDDEN): skipped (nothing to draw; camera sits on the car anyway).
- Broken/short ChainNode chain mid-teardown: skip that car (return without fatal) — unlike AlignWheelsAndHeadOnCar which DoFatalAlerts; a render path must never fatal.
- Car eliminated / objNode==nil / CType==INVALID_NODE_FLAG: slot skipped.
- Track-completed cooldown / gGameOver: objNodes may be tearing down — Begin's per-car nil/CType/chain guards make it safe; optionally skip entirely when gTrackCompleted.
- Re-entrancy: sExtrapActive asserts Begin/End pairing; if End is ever skipped the next Begin asserts (debug) — keep the caller's wrap unconditional on the 'ex' boolean.
- Multiple consecutive held frames: each frame saves the TRUE baseline, extrapolates from the FIXED sLastRealStepTick, and restores — no error compounding; the first real packet after the gap resumes from untouched sim state.
- Airborne car with large downward Delta.y over 100 ms can visibly clip terrain; acceptable for <=100 ms (optional: clamp extrapolated Coord.y to GetTerrainY()).
- Shadow uses terrain-conform (UpdateShadow→RotateOnTerrain) at the extrapolated x/z — correct height; if shadow re-anchor is deferred, a <=100 ms car/shadow separation is the only artifact.
- Split-screen: net games render a single local pane; no per-pane interaction. Single-player/split (non-net) never reaches this path (gIsNetworkClient guard).


**Testing.** "DETERMINISM (the critical test): force synthetic held frames by dropping every Nth packet from Stage 3's host-packet ring; run a 20-race profile-A soak with gEnableVisualExtrapolation=true and assert ZERO kNetSequence_SeedDesync and zero spurious fatals — proves the save/restore is bit-exact (a missing restored field would desync the very next real step). Add a DEBUG memcmp assertion: snapshot every touched node's BaseTransformMatrix before Begin into a scratch copy, and after End memcmp-equal it. SMOOTHNESS/A-B: under netem profile C (5GHz spiky, scripted 200 ms downlink bursts) and profile D (congested 2.4GHz), toggle gEnableVisualExtrapolation and compare: with it ON, remote cars glide through the gap instead of freezing; record video to confirm. METRICS (extend Stage 0 CSV/HUD): per-second count of extrapolated frames, mean/p95 extrapSec, and a 'frame at cap' counter. PERF: assert the added per-held-frame cost (<=6 cars × {1 UpdateObjectTransforms + 1 invert + 6 multiplies + 1 UpdateShadow}) is <0.5 ms on the weakest Android target; held frames are rare so amortized cost is negligible. VISUAL/RIGIDITY: eyeball that wheels/head stay glued to the body during extrapolation (delta re-anchor correctness) and that own-car/camera behavior matches gExtrapolateLocalCar. ACCEPTANCE (profile C): with extrapolation ON, a 200 ms downlink burst produces a smooth coast rather than a visible freeze, and the post-gap seed check still passes."


**Risks:**
- Hard dependency on Stage 3: needs the empty-ring hold-last branch, StepGameSimulation() factoring, and the gClientHeldThisFrame signal. If Stage 3 named these differently the two Main.c hook points shift — integration is mechanical but must follow Stage 3's actual structure.
- Restore incompleteness = desync risk: this is the only way Stage 6 could break lockstep. Mitigated structurally (snapshot/restore covers Coord, Rot.y, all 6+shadow BaseTransformMatrix and their BaseTransformObject mirrors via SetObjectTransformMatrix) and by the DEBUG memcmp assertion in the test plan. Determinism is otherwise safe by construction (no gSimulationFrame, no synced-RNG draw touched).
- Own-car slide if gExtrapolateLocalCar is enabled while the camera is frozen: the local car would drift across a static frame. Default OFF avoids it; enabling it correctly would require also dead-reckoning the camera (extrapolate playerInfo->coord + save/restore cameraRingRot and playerInfo->camera) — documented as a future option, not in scope.
- Ground-FX lag: skid marks (UpdateSkidMarks, sim-time) and wheel-debris particles (MoveParticleGroups, sim-time) are NOT re-derived from car Coord at draw, so they stay put while the car coasts — a <=100 ms cosmetic mismatch. Shadow IS re-anchored; skid/particle lag is accepted as polish-of-polish.
- DeltaRot.y over-rotation near the cap on a hard turn, or Delta over-translation, can produce a small snap-back when the real packet lands; bounded by <=100 ms × velocity and further softened by the kept 0.2 rubber-band (NetHigh.c:889-907).
- Matrix-convention assumption (Multiply(A,B)=B·A) is load-bearing for the delta re-anchor; verified here against Objects.c:1272 and Player_Car.c:3446, but if the math lib is ever changed the wheels would visibly detach — caught instantly by the rigidity eyeball test.
- Scope creep: synthesis flags this as ship-only-if-needed. Keep gEnableVisualExtrapolation default OFF; if Stage 5 hold-last playtests are acceptable, this stage can be cut entirely.
