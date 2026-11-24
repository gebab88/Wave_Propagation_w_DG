#!/usr/bin/env bash
# Tests for the in-process video rendering (frames streamed into ffmpeg).
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

make Release >/dev/null

BIN="$repo_root/bin/Release/DiscontinousGalerkin"
SMOKE_CFG="$repo_root/tests/linux-smoke-config.yaml"
DEFAULT_CFG="$repo_root/config.yaml"

work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
cd "$work_dir"

failures=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1: $2"; failures=$((failures + 1)); }

test_frame_count_matches() {
    local name="${FUNCNAME[0]}" d="$work_dir/count"
    mkdir -p "$d"; cd "$d"
    local out; out="$(timeout 60 "$BIN" "$SMOKE_CFG" </dev/null 2>&1)" || {
        fail "$name" "solver exited non-zero"; cd "$work_dir"; return; }
    local n; n="$(sed -n 's/^Rendering \([0-9][0-9]*\) frames at 60 fps$/\1/p' <<<"$out" | head -n1)"
    if [[ -z "$n" ]]; then fail "$name" "no 'Rendering <n> frames at 60 fps' line"; cd "$work_dir"; return; fi
    if [[ ! -s output.mp4 ]]; then fail "$name" "output.mp4 missing or empty"; cd "$work_dir"; return; fi
    local probe; probe="$(ffprobe -v error -count_frames -select_streams v:0 \
        -show_entries stream=nb_read_frames,r_frame_rate,width,height -of csv=p=0 output.mp4)" || probe="ffprobe failed"
    local pngs; pngs="$(find . -maxdepth 1 -name '*.png' | wc -l | tr -d ' ')"
    cd "$work_dir"
    if [[ "$probe" != "800,600,60/1,$n" ]]; then fail "$name" "ffprobe said '$probe', expected '800,600,60/1,$n'"; return; fi
    if [[ "$pngs" != "0" ]]; then fail "$name" "$pngs PNG files written"; return; fi
    pass "$name"
}

test_ffmpeg_failure_reported() {
    local name="${FUNCNAME[0]}" d="$work_dir/fail" fake="$work_dir/fakebin"
    mkdir -p "$d" "$fake"
    printf '#!/bin/sh\nexit 1\n' > "$fake/ffmpeg"; chmod +x "$fake/ffmpeg"
    cd "$d"
    local out rc=0
    out="$(PATH="$fake:$PATH" timeout 60 "$BIN" "$SMOKE_CFG" </dev/null 2>&1)" || rc=$?
    cd "$work_dir"
    if [[ "$rc" != 0 ]]; then fail "$name" "exit code $rc"; return; fi
    [[ "$out" == *'Video encoding failed'* ]] || { fail "$name" "missing 'Video encoding failed'"; return; }
    [[ "$out" == *'Plotting has finished!'* ]] || { fail "$name" "missing 'Plotting has finished!'"; return; }
    pass "$name"
}

test_memory_bounded() {
    local name="${FUNCNAME[0]}" d="$work_dir/mem"
    if [[ "$(uname -s)" != "Darwin" ]]; then
        echo "PASS $name (skipped: needs macOS /usr/bin/time -l)"; return
    fi
    mkdir -p "$d"; cd "$d"
    local log="$d/time.log"
    local cfg="$d/config.yaml"
    sed 's/^\( *EnablePlotting:\).*/\1 true/' "$DEFAULT_CFG" >"$cfg"
    local rc=0
    /usr/bin/time -l "$BIN" "$cfg" </dev/null >"$d/out.log" 2>"$log" || rc=$?
    cd "$work_dir"
    if [[ "$rc" -ne 0 ]]; then fail "$name" "solver exited with status $rc"; return; fi
    if ! grep -q 'Video written to' "$d/out.log"; then fail "$name" "no 'Video written to' in output"; return; fi
    local rss; rss="$(sed -n 's/^ *\([0-9][0-9]*\)  *maximum resident set size.*/\1/p' "$log" | head -n1)"
    if [[ -z "$rss" ]]; then fail "$name" "no max RSS in time output"; return; fi
    if (( rss < 1073741824 )); then pass "$name (max RSS $rss bytes)"
    else fail "$name" "max RSS $rss bytes >= 1 GiB"; fi
}

test_frame_count_matches
test_ffmpeg_failure_reported
test_memory_bounded

if [[ "$failures" -ne 0 ]]; then
    echo "$failures video render test(s) failed."
    exit 1
fi
echo "All video render tests passed."
