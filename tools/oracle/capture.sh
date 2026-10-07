#!/bin/sh
# Run an oracle capture script against the real arcade code in MAME, headless.
#
# Usage: tools/oracle/capture.sh <script.lua> <output-file> [passes] [seconds]
#
# Captures are derived from copyrighted ROM code/data: they stay local under
# build/ (gitignored) and are regenerated from your own ROM set, never committed.
#
# MAME (default /Users/dakiva/dev/mame/mame) and ROMPATH (default
# /Users/dakiva/dev/mame/roms) can be overridden from the environment.
set -eu

script=$1
out=$2
passes=${3:-600}
seconds=${4:-120}

MAME=${MAME:-/Users/dakiva/dev/mame/mame}
ROMPATH=${ROMPATH:-/Users/dakiva/dev/mame/roms}

work=$(mktemp -d "${TMPDIR:-/tmp}/ab2oracle.XXXXXX")
trap 'rm -rf "$work"' EXIT

# Fresh NVRAM/config every run: boot is deterministic only from a clean state.
status=0
# Optional read-only instruction-boundary snapshots for internal ALU tails.
set --
if [ "${AB2_ORACLE_CALLER_SNAPSHOTS:-0}" = 1 ]; then
    set -- -debug -debugger none
fi
AB2_ORACLE_OUT=$(cd "$(dirname "$out")" && pwd)/$(basename "$out") \
AB2_ORACLE_PASSES=$passes \
AB2_ORACLE_SCRIPT_DIR=$(cd "$(dirname "$script")" && pwd) \
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
"$MAME" aburner2 -rompath "$ROMPATH" \
    -video none -sound none -nothrottle -skip_gameinfo -seconds_to_run "$seconds" \
    -nvram_directory "$work/nvram" -cfg_directory "$work/cfg" \
    -snapshot_directory "$work/snap" -diff_directory "$work/diff" \
    "$@" -autoboot_script "$script" >"$work/mame.log" 2>&1 || status=$?
grep -v -e NSXPC -e hiservices -e 'Connection invalid' -e "doesn't make much sense" "$work/mame.log" || true
# MAME can exit successfully after a Lua callback failed. A partial capture
# is not success, and the pipeline must not hide MAME's own exit status.
if grep -q '\[LUA ERROR\]' "$work/mame.log"; then exit 1; fi
if [ "$status" -eq 0 ] && ! grep -q '^captured ' "$work/mame.log"; then
    echo "Capture did not complete before the time limit." >&2
    exit 1
fi
exit "$status"
