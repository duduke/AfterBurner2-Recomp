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
AB2_ORACLE_OUT=$(cd "$(dirname "$out")" && pwd)/$(basename "$out") \
AB2_ORACLE_PASSES=$passes \
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
"$MAME" aburner2 -rompath "$ROMPATH" \
    -video none -sound none -nothrottle -skip_gameinfo -seconds_to_run "$seconds" \
    -nvram_directory "$work/nvram" -cfg_directory "$work/cfg" \
    -snapshot_directory "$work/snap" -diff_directory "$work/diff" \
    -autoboot_script "$script" 2>&1 \
  | grep -v -e NSXPC -e hiservices -e 'Connection invalid' -e "doesn't make much sense" || true
