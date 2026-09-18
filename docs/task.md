# Post-v3.1.1 review tasks

Fresh-review backlog originally recorded at `7d881127` / tag `v3.1.1`.

## Assessment and implementation order (2026-09-17)

Reviewed against `master` at `e10e692d`. The repository has no GitHub issues
(open or closed) and no open PRs as of this assessment. PR #13's client terrain
residency fix is already merged and does not replace any task below.

The reported defects remain actionable. Keep the list, with these refinements:

- Combine each fix with its regression tests in the same PR. The two offscreen
  smoke entries describe one production-game test suite, built with sanitizers.
- CLI startup currently exposes race/practice/host paths, not battle-mode
  selection. Test the supported combinations and rejection of battle track IDs;
  do not add a new CLI mode merely to satisfy the smoke-test wording.
- Keep refresh rate `0` as an unknown-monitor sentinel in character messages.
  Configuration and final host sync must carry an actual supported FPS. Validate
  client readiness messages according to their distinct wire semantics too.
- Measure handshake deadlines from acceptance, so trickled bytes cannot renew
  a silent peer's reservation. Readiness deadlines must preserve ready peers,
  sparse player IDs, and deterministic departure handling.
- Asset validation must reject missing/short required resources before use, as
  well as out-of-range values. Race checkpoint validation must also cover a
  header that promises checkpoints but has no checkpoint resource.
- Android content identity must be deterministic across both build wrappers and
  change when asset bytes change without a version bump. Test failed extraction
  and retry as well as the same-version update.
- Build production releases from a pushed version tag. Upload and verify the
  complete artifact set while the release is a draft; publication is the last
  step. Manual smoke builds must remain separate from publication.
- Validate Android wrapper caching on both shell and PowerShell paths. Add
  simulator and Android release/x86_64 CI coverage; evaluate expensive package
  jobs separately from the already-covered common Linux packaging path.
- The gl4es fix belongs in the forked dependency, with a published commit and an
  updated gitlink here; its Apple test must force the lookup archive member into
  the link, not just compile declarations.

Implementation steps: documentation reconciliation; startup and gameplay/input;
network message invariants; resource validation; network lifetime/readiness and
discovery; Android extraction/build caching; release publication; remaining CI
and Apple dependency coverage. Each step requires a PR, passing CI, and a clean
bot review before merge. Unchecked entries are open backlog items; checked entries represent completed tasks.

## Priority: correctness and availability

- [x] Migrate legacy tvOS saves from Caches into persistent storage when no `NSUserDefaults` copy exists; test first launch, migration, corrupt data, and a failed persistent write without losing the legacy file.
- [x] Clear the network session after cancelling host setup; the new host smoke caught a stale `gNetGame` use-after-free during shutdown.
- [x] Restrict `--track` race/practice/host startup paths to `NUM_RACE_TRACKS`.
- [x] Reject race-mode playfields with zero checkpoints before player/checkpoint initialization.
- [x] Add an offscreen sanitizer smoke test covering every CLI-selectable track/mode combination, including the current `--track 10` regression. Practice runs simulate/render each track; host runs exercise the lobby. Peer/gameplay barriers remain part of the network tests below.
- [x] Define one shared minimum network FPS and use it consistently for config, character refresh-rate negotiation, host-control validation, and final sync.
- [x] Reject or ignore positive client refresh rates below the supported network minimum; add boundary tests for 0, 1, 8, 9, 1000, and 1001.
- [x] Add semantic validation for `NetSyncMessage`, including `targetFPS`, reserved padding, and the same FPS invariant used by the earlier configuration messages.
- [x] Expire and recycle `AwaitingHandshake` TCP slots after a bounded lobby handshake deadline.
- [x] Add a bounded vehicle-selection readiness deadline and let the host remove only nonready peers instead of waiting indefinitely or cancelling the entire session.
- [x] Replace the level-ready barrier's session-wide fatal timeout with per-peer readiness tracking and removal of only stalled peers.
- [x] Add loopback/state-machine tests for silent handshakes, sparse lobby churn, stalled character selection, delayed level readiness, send-ring overflow, simultaneous leaves, leave while paused, battle victory after departures, and two games in one process.
- [x] Rework release production flow so builds run from a pushed version tag or draft release and the public GitHub Release is published only after every required artifact and checksum succeeds.
- [x] Add a release-workflow failure test proving that a failed matrix job cannot leave a public empty or incomplete release.
- [x] Generate a deterministic Android asset-manifest/content hash and use it to invalidate extracted assets instead of relying only on `GAME_VERSION` and one sentinel file.
- [x] Test same-version Android reinstall/update behavior by changing an asset without bumping the marketing version and verifying the extracted bytes are refreshed.

## Priority: resource and data hardening

- [x] Add central playfield-header validation for signed counts, terrain dimensions, tile size, supported maxima, and multiplication/allocation overflow before using those values.
- [x] Persist the loaded tile-attribute count and validate every `Layr` tile ID before indexing `gTileAttribList`.
- [x] Make an illegal terrain-item type fail safely before indexing `gTerrainItemAddRoutines`; do not continue after the alert.
- [x] Require every fence to have at least two nubs, validate nub counts before allocation, check resource handles before locking them, and allocate nub storage using the correct element type.
- [x] Bound skeleton `numAnims` by `MAX_ANIMS`, animation-event counts by `MAX_ANIM_EVENTS`, and validate all bone/index/keyframe resource sizes and referenced indices before copying.
- [x] Add malformed playfield and skeleton fixtures covering oversized/negative counts, invalid tile and item IDs, zero-nub fences, excess animations/events, short resources, and overflow-sized allocations.

## Priority: gameplay and input correctness

- [x] Mask keyboard need states with `KEYSTATE_ACTIVE_BIT` in analog input lookup so `KEYSTATE_UP` and `KEYSTATE_IGNOREHELD` do not produce full-scale input.
- [x] Add input-state tests covering press, hold, release, invalidation, keyboard fallback, and mixed keyboard/gamepad analog behavior.
- [x] Stop checkpoint processing for players whose race is already complete and make `PlayerCompletedRace` idempotent so race times/UI cannot be recorded twice during cooldown.
- [x] Saturate or cap gong weapon-quantity doubling so signed `powQuantity` cannot overflow.
- [x] Pass the bird-bomb projectile, not its thrower, to `MakeBombExplosion` so thrower attribution and announcer behavior are preserved.

## Priority: network polish

- [x] Clear a player's degraded-connection badge when that player is kicked or converted to a bot.
- [x] Timestamp lobby advertisements, expire stale discovery entries, and try another live entry when joining the first result fails.
- [x] Initialize `HostSendGameConfigInfo`'s return status defensively even when no client send occurs.

## Priority: build, CI, release UX, and supply chain

- [x] Build the full `CroMagRally` target in sanitizer CI, not only selected test executables.
- [x] Add a bounded offscreen/headless sanitizer boot smoke to exercise production startup, resource loading, and gameplay initialization.
- [x] Fix Android wrapper NDK-cache detection to compare the normalized toolchain path/revision or a wrapper-owned NDK stamp instead of requiring a missing `CMAKE_ANDROID_NDK` cache key.
- [x] Add a two-run Android wrapper test proving a matching NDK preserves the incremental CMake build tree.
- [x] Label ad-hoc-signed, unnotarized macOS release artifacts as unsigned and disclose that status prominently in release notes.
- [x] Add Gradle wrapper validation in CI while retaining the pinned distribution checksum.
- [x] Validate `build_ios.sh` and `build_tvos.sh` arguments explicitly and reject values other than `device` or `simulator`.
- [x] Add CI coverage for the iOS/tvOS simulator paths used by the scripts' defaults; add Android x86_64/release PR coverage. Arch/Flatpak and Windows ARM64 remain release gates because they require separate build environments; DEB/RPM already cover the common Linux install path on PRs.
- [x] Replace the gl4es Apple declaration-only aliases for `gl4es_glEnableClientStatei` and `gl4es_glDisableClientStatei` with real forwarding definitions.
- [x] Add an Apple gl4es link test that references `gl4es_GetProcAddress` and both client-state symbols so future shared/lookup configurations cannot regress silently.

## Priority: documentation

- [x] Update `docs/REVIEW.md` to remove the now-false claim that tvOS persistence still uses purgeable cache storage.
- [x] Reconcile `docs/SUBMODULE-AUDIT.md` wording so its verdict acknowledges the documented Pomme runtime changes as well as portability changes.

## Holistic review follow-up (2026-09-18)

Reviewed `72eb0b1d46b4c93340a3d43744b9ae401a1e0f63` after the preceding
backlog was completed. See [the review analysis](REVIEW-2026-09-18.md) for
evidence, validation results, and limitations. These entries are confirmed
defects; non-defect proposals remain outside this task list pending approval.
Pair each correction with the focused regression coverage described in its issue.

Implementation decisions: retain the unpublished CMR8 networking level. Race-end
cooldown freezes during pause, as approved on 2026-09-18. The #26 implementation
shares simulation completion between gameplay and the client pause menu; the
local sanitizer build, 11 CTest suites, and 30 startup smoke cases pass.
Checked entries below have an implementation and local regression coverage;
their PRs must still pass CI and bot review before merge.

- [x] [#26](https://github.com/jm2/CroMagRally/issues/26) — Advance network race-end cooldown according to synchronized simulation progress; keep completion consistent across client hold/catch-up renders and freeze during pause.
- [ ] [#27](https://github.com/jm2/CroMagRally/issues/27) — Give terrain LZSS decoding a destination-capacity contract, check before emitting bytes, and reject incomplete input before texture use.
- [ ] [#28](https://github.com/jm2/CroMagRally/issues/28) — Validate BG3D material/geometry counts, references, required arrays, read completion, and texture byte sizes before allocation, indexing, or upload.
- [ ] [#29](https://github.com/jm2/CroMagRally/issues/29) — Handle exact and near-coincident AI path origins without returning NaN directions.
- [ ] [#30](https://github.com/jm2/CroMagRally/issues/30) — Preserve the preceding valid direction at the final AI path point instead of storing a zero vector.
- [ ] [#31](https://github.com/jm2/CroMagRally/issues/31) — Include all active local listeners in sound attenuation and use each listener's position/orientation for stereo mixing.
- [ ] [#32](https://github.com/jm2/CroMagRally/issues/32) — Save the active session difficulty in race records instead of the local preference.
- [ ] [#33](https://github.com/jm2/CroMagRally/issues/33) — Declare the external SDL3 runtime dependency for system-SDL Debian packages and verify both bundled and external-SDL package layouts.
- [ ] [#34](https://github.com/jm2/CroMagRally/issues/34) — Correct BG3D group-stack popping and verify nested and sibling group handling; this defect is latent with the current asset set.
