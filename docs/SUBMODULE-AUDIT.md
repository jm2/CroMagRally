# Forked submodule audit — extern/Pomme, extern/gl4es

Audit of the two forked submodules against their upstreams, June 2026. Each fork's
delta was fetched, diffed against the upstream merge-base, and reviewed for
security/memory-safety, correctness/regression, and build-portability (Windows ARM64,
Android, iOS, tvOS).

## Verdict

**Both forks are safe to keep as-is for the game's current targets.** Every change is
portability-only; no security-relevant or runtime game logic was altered. The only
action items are maintenance/divergence hygiene and one incomplete-in-the-general-case
gl4es fix that is dormant today.

| Submodule | Fork delta | Verdict |
|-----------|-----------|---------|
| `extern/Pomme` (jm2/Pomme @ 06607ff) | 2 commits on a clean, linear upstream-master base (2 ahead, 0 behind) | Functionally safe; needs hygiene cleanup before the next upstream bump |
| `extern/gl4es` (jm2/gl4es @ 1f667c5) | 1 commit, direct child of upstream master | Safe for the current static iOS/tvOS build; complete the fix to remove a latent link landmine |

## Pomme

Upstream: `jorio/Pomme`. The two fork commits are pure portability:

1. **macOS 26.2 compile fixes** (69b0d2f) — newer macOS SDKs now globally define the
   classic Toolbox symbols Pomme also declares, so the fork guards Pomme's own
   `Point`/`Rect`/`Fixed*`/`NumVersion`/integer types under `#if !defined(__MACTYPES__)`,
   guards `noErr`/`nil`, and adds `(char*)` casts where Apple's `Str255` (unsigned) meets
   `snprintf`/`fs::path`.
2. **Android prefs path** (06607ff) — adds an Android branch to `FindFolder()` that
   resolves the preferences directory via `SDL_GetPrefPath`.

The privileged code paths — the `FSSpec` directory-ID bounds check, AppleDouble (`.rsrc`)
resource-fork parsing, case-insensitive host-path mapping, and `Str255` sizing — are
byte-identical to upstream apart from reformatting and the well-defined casts. No
memory-safety regression, no UB, no parser change.

**Hygiene items (address before the next upstream Pomme bump, not blockers):**

- **~1000-line clang-format churn across 6 files.** The raw diff is 1039/1064 lines but the
  semantic delta is ~30 lines (`git diff -w`). Because whitespace/brace/include order was
  rewritten wholesale, any upstream edit to these files will conflict line-by-line and can
  mask a real fix. *Fix:* isolate the semantic edits into a minimal commit and drop the
  cosmetic churn, or commit upstream's own `.clang-format` and apply it consistently.
- **CMR-specific coupling in a reusable file.** `Files.cpp` now hard-includes the Android
  SDL3 header and hardcodes the prefs org/app strings (`io.jor`/`cromagrally`); upstream
  Pomme was SDL-free. It only links because CMake exposes SDL3 under a build condition.
  *Fix (optional):* pass the prefs path / org+app into Pomme, or at least gate the SDL
  include behind the condition that guarantees SDL is linked.
- **Broad `__MACTYPES__` guard.** A single guard (PommeTypes.h:10–108) suppresses not only
  the documented types but the whole "Basic system types" (`OSErr`, `OSType`, `Ptr`,
  `Handle`, `Size`) and "String types" (`Str15`…`Str255`) blocks on Apple, betting that
  `<MacTypes.h>` replaces every one. It compiles only because the game doesn't use the few
  symbols MacTypes.h omits. *Fix:* narrow to per-symbol `#ifndef` fences, or add a
  `static_assert` that the relied-upon types exist so a missing replacement fails loudly.

## gl4es

Upstream: `ptitSeb/gl4es`. One fork commit (1f667c5): a 6-line Apple branch in the
`AliasDecl` macro (attributes.h) so Apple-clang (which doesn't support `alias` attributes
on Darwin) compiles, mirroring upstream's existing Apple handling of `AliasExport`. It's a
no-op for Windows ARM64 (MSVC) and Android (GNUC alias path).

**Medium — incomplete in the general case (dormant today).** The Apple branch leaves
`gl4es_glEnableClientStatei`/`glDisableClientStatei` declaration-only. Those references
live only in `gl_lookup.o`, which **nothing in the game pulls in**: the game uses
`SDL_GL_GetProcAddress` (Boot.cpp:302), the gl4es `GetProcAddress` callers
(GLX/EGL/AGL `lookup.c`) are Linux-gated and excluded on Apple, and iOS/tvOS force a
static lib — so the archive member is dead-stripped and the link succeeds. Verified by
linking, not just compiling.

A real link failure would only appear if gl4es were built **shared** on Apple, or a
`GetProcAddress` path were enabled. *Fix to remove the landmine:* give the Apple branch a
real definition (a forwarding wrapper calling `OLD`, or a Darwin asm symbol alias) instead
of a bare prototype, and keep gl4es **static** on Apple. Validate by linking.

---

*Full structured findings: `cromag-review/submodule-audit-output.json`.*
