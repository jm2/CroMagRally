# Post-v3.1.1 review tasks

Fresh-review backlog for HEAD `7d881127` / tag `v3.1.1`.

## Priority: correctness and availability

- [ ] Restrict `--track` race/practice/host startup paths to `NUM_RACE_TRACKS`.
- [ ] Reject race-mode playfields with zero checkpoints before player/checkpoint initialization.
- [ ] Add an offscreen sanitizer smoke test covering every CLI-selectable track/mode combination, including the current `--track 10` regression.
- [x] Define one shared minimum network FPS and use it consistently for config, character refresh-rate negotiation, host-control validation, and final sync.
- [x] Reject or ignore positive client refresh rates below the supported network minimum; add boundary tests for 0, 1, 8, 9, 1000, and 1001.
- [x] Add semantic validation for `NetSyncMessage`, including `targetFPS`, reserved padding, and the same FPS invariant used by the earlier configuration messages.
- [ ] Expire and recycle `AwaitingHandshake` TCP slots after a bounded lobby handshake deadline.
- [ ] Add a bounded vehicle-selection readiness deadline and let the host remove only nonready peers instead of waiting indefinitely or cancelling the entire session.
- [ ] Replace the level-ready barrier's session-wide fatal timeout with per-peer readiness tracking and removal of only stalled peers.
- [ ] Add loopback/state-machine tests for silent handshakes, sparse lobby churn, stalled character selection, delayed level readiness, send-ring overflow, simultaneous leaves, leave while paused, and two games in one process.
- [ ] Rework release production flow so builds run from a pushed version tag or draft release and the public GitHub Release is published only after every required artifact and checksum succeeds.
- [ ] Add a release-workflow failure test proving that a failed matrix job cannot leave a public empty or incomplete release.
- [ ] Generate a deterministic Android asset-manifest/content hash and use it to invalidate extracted assets instead of relying only on `GAME_VERSION` and one sentinel file.
- [ ] Test same-version Android reinstall/update behavior by changing an asset without bumping the marketing version and verifying the extracted bytes are refreshed.

## Priority: resource and data hardening

- [ ] Add central playfield-header validation for signed counts, terrain dimensions, tile size, supported maxima, and multiplication/allocation overflow before using those values.
- [ ] Persist the loaded tile-attribute count and validate every `Layr` tile ID before indexing `gTileAttribList`.
- [ ] Make an illegal terrain-item type fail safely before indexing `gTerrainItemAddRoutines`; do not continue after the alert.
- [ ] Require every fence to have at least two nubs, validate nub counts before allocation, check resource handles before locking them, and allocate nub storage using the correct element type.
- [ ] Bound skeleton `numAnims` by `MAX_ANIMS`, animation-event counts by `MAX_ANIM_EVENTS`, and validate all bone/index/keyframe resource sizes and referenced indices before copying.
- [ ] Add malformed playfield and skeleton fixtures covering oversized/negative counts, invalid tile and item IDs, zero-nub fences, excess animations/events, short resources, and overflow-sized allocations.

## Priority: gameplay and input correctness

- [ ] Mask keyboard need states with `KEYSTATE_ACTIVE_BIT` in analog input lookup so `KEYSTATE_UP` and `KEYSTATE_IGNOREHELD` do not produce full-scale input.
- [ ] Add input-state tests covering press, hold, release, invalidation, keyboard fallback, and mixed keyboard/gamepad analog behavior.
- [ ] Stop checkpoint processing for players whose race is already complete and make `PlayerCompletedRace` idempotent so race times/UI cannot be recorded twice during cooldown.
- [ ] Saturate or cap gong weapon-quantity doubling so signed `powQuantity` cannot overflow.
- [ ] Pass the bird-bomb projectile, not its thrower, to `MakeBombExplosion` so thrower attribution and announcer behavior are preserved.

## Priority: network polish

- [ ] Clear a player's degraded-connection badge when that player is kicked or converted to a bot.
- [ ] Timestamp lobby advertisements, expire stale discovery entries, and try another live entry when joining the first result fails.
- [x] Initialize `HostSendGameConfigInfo`'s return status defensively even when no client send occurs.

## Priority: build, CI, release UX, and supply chain

- [ ] Build the full `CroMagRally` target in sanitizer CI, not only selected test executables.
- [ ] Add a bounded offscreen/headless sanitizer boot smoke to exercise production startup, resource loading, and gameplay initialization.
- [ ] Fix Android wrapper NDK-cache detection to compare the normalized toolchain path/revision or a wrapper-owned NDK stamp instead of requiring a missing `CMAKE_ANDROID_NDK` cache key.
- [ ] Add a two-run Android wrapper test proving a matching NDK preserves the incremental CMake build tree.
- [ ] Label ad-hoc-signed, unnotarized macOS release artifacts as unsigned and disclose that status prominently in release notes.
- [ ] Add Gradle wrapper validation in CI while retaining the pinned distribution checksum.
- [ ] Validate `build_ios.sh` and `build_tvos.sh` arguments explicitly and reject values other than `device` or `simulator`.
- [ ] Add CI coverage for the iOS/tvOS simulator paths used by the scripts' defaults; consider PR coverage for Android x86_64/release and currently release-gated package paths.
- [ ] Replace the gl4es Apple declaration-only aliases for `gl4es_glEnableClientStatei` and `gl4es_glDisableClientStatei` with real forwarding definitions.
- [ ] Add an Apple gl4es link test that references `gl4es_GetProcAddress` and both client-state symbols so future shared/lookup configurations cannot regress silently.

## Priority: documentation

- [ ] Update `docs/REVIEW.md` to remove the now-false claim that tvOS persistence still uses purgeable cache storage.
- [ ] Reconcile `docs/SUBMODULE-AUDIT.md` wording so its verdict acknowledges the documented Pomme runtime changes as well as portability changes.
