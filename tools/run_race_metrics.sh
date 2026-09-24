#!/bin/bash
# Race-metrics soak (Linux, offscreen video): run whole headless practice races with the
# smoke-only soak flags (--smoke-autopilot, --smoke-cars, --smoke-fixed-fps, --smoke-seed,
# --smoke-until-finish, --smoke-metrics) and keep each race's METRICS lines for
# tools/analyze_race_metrics.py.
#
# Usage: tools/run_race_metrics.sh <build dir> <out dir> [tracks] [car counts] [fps list] [jobs]
#   build dir   directory holding the CroMagRally binary and its Data folder
#   out dir     receives t<track>_n<cars>_f<fps>.log (METRICS lines and errors) per race,
#               plus full/ with each race's complete log
#   tracks      race tracks 1-9                    (default: "1 2 3 4 5 6 7 8 9")
#   car counts  cars per race, 1..MAX_PLAYERS      (default: "6")
#   fps list    fixed simulation rates             (default: "50 60 72")
#   jobs        races run in parallel              (default: 4)
#
# Env knobs:
#   SEED        synced RNG seed for every race     (default: 12345)
#   FRAMES      frame cap per race  (default: 15 minutes at the race's fps, at most 100000)
#   TIMEOUT     wall-clock limit per race, seconds (default: 1800)
#   GAME_ARGS   extra game arguments, e.g. "--car 3"
#
# A pinned seed with a fixed timestep makes a race exactly repeatable, so vary the fps
# list (e.g. 50/60/72) for independent samples. Races whose log already holds a
# METRICS race line are skipped, so an interrupted soak resumes where it stopped.
# Each race uses a fresh preferences directory under ${TMPDIR:-/var/tmp}.
# The exit status is nonzero if any race did not produce its metrics or logged a
# sanitizer error.

set -u

if [ "$#" -lt 2 ] || [ "$#" -gt 6 ]; then
    awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0" >&2
    exit 2
fi

BUILD=$(cd "$1" && pwd) || exit 2
if [ ! -x "$BUILD/CroMagRally" ]; then
    echo "No CroMagRally binary in $BUILD" >&2
    exit 2
fi
mkdir -p "$2/full" || exit 2
OUT=$(cd "$2" && pwd)
TRACKS=${3:-1 2 3 4 5 6 7 8 9}
CARS=${4:-6}
FPS=${5:-50 60 72}
JOBS=${6:-4}
export BUILD OUT
export SEED=${SEED:-12345} FRAMES=${FRAMES:-} TIMEOUT=${TIMEOUT:-1800} GAME_ARGS=${GAME_ARGS:-}
export SCRATCH=${TMPDIR:-/var/tmp}

run_race() {
    local track=$1 cars=$2 fps=$3
    local name=t${track}_n${cars}_f${fps}
    local log=$OUT/$name.log full=$OUT/full/$name.log
    local frames=${FRAMES:-$((fps * 900 < 100000 ? fps * 900 : 100000))}

    if grep -q "METRICS race" "$log" 2>/dev/null; then
        echo "$name: done earlier"
        return 0
    fi

    local prefs
    prefs=$(mktemp -d "$SCRATCH/cmr-metrics-XXXXXX") || return 1
    # shellcheck disable=SC2086 # GAME_ARGS is a word list
    (cd "$BUILD" && env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 \
        LP_NUM_THREADS="${LP_NUM_THREADS:-2}" XDG_CONFIG_HOME="$prefs" XDG_CACHE_HOME="$prefs" \
        ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1}" \
        timeout "$TIMEOUT" ./CroMagRally --track "$track" --no-vsync --smoke-test-frames "$frames" \
            --smoke-cars "$cars" --smoke-fixed-fps "$fps" --smoke-seed "$SEED" \
            --smoke-autopilot --smoke-until-finish --smoke-metrics $GAME_ARGS) > "$full" 2>&1
    local status=$?
    rm -rf "$prefs"

    grep -E "METRICS|SMOKE:|Fatal|runtime error|Sanitizer|NONFINITE" "$full" > "$log"
    local race
    race=$(grep -m1 "METRICS race" "$log")
    if [ "$status" -ne 0 ] || [ -z "$race" ] || grep -qE "runtime error|Sanitizer" "$log"; then
        echo "$name: FAILED (exit $status) -- see $full"
        rm -f "$log"		# not "done": a rerun retries it
        return 1
    fi
    echo "$name: ${race#*METRICS race }"
}
export -f run_race

for t in $TRACKS; do for n in $CARS; do for f in $FPS; do echo "$t $n $f"; done; done; done \
    | xargs -P "$JOBS" -L1 bash -c 'run_race "$@"' run_race
