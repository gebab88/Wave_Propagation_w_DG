#!/usr/bin/env bash
# Tests for the in-process video rendering (frames streamed into ffmpeg).
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

make Release >/dev/null

BIN="$repo_root/bin/Release/DiscontinousGalerkin"
SMOKE_CFG="$repo_root/tests/linux-smoke-config.yaml"
DEFAULT_CFG="$repo_root/config_example.yaml"   # versioned defaults, not the local config.yaml

work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
cd "$work_dir"

is_windows=0; case "$(uname -s)" in MINGW*|UCRT64*|CLANG64*|CLANGARM64*|MSYS*) is_windows=1 ;; esac

failures=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1: $2"; failures=$((failures + 1)); }

test_frame_count_matches() {
    local name="${FUNCNAME[0]}" d="$work_dir/count"
    mkdir -p "$d"; cd "$d"
    local out; out="$(timeout 60 "$BIN" "$SMOKE_CFG" </dev/null 2>&1 | tr -d '\r')" || {
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
    if [[ "$is_windows" == 1 ]]; then
        # cmd.exe (used by _popen) finds ffmpeg.cmd through PATHEXT, not a shell script.
        printf '@exit /b 1\r\n' > "$fake/ffmpeg.cmd"
    else
        printf '#!/bin/sh\nexit 1\n' > "$fake/ffmpeg"; chmod +x "$fake/ffmpeg"
    fi
    cd "$d"
    local out rc=0
    out="$(PATH="$fake:$PATH" timeout 60 "$BIN" "$SMOKE_CFG" </dev/null 2>&1)" || rc=$?
    cd "$work_dir"
    if [[ "$rc" != 0 ]]; then fail "$name" "exit code $rc"; return; fi
    [[ "$out" == *'Video encoding failed'* ]] || { fail "$name" "missing 'Video encoding failed'"; return; }
    [[ "$out" == *'Plotting has finished!'* ]] || { fail "$name" "missing 'Plotting has finished!'"; return; }
    pass "$name"
}

# set_key <in> <out> <key> <value>: copies a config, replacing the line of
# <key> or appending it when the config has none (portable: no sed -i).
set_key() {
    local in="$1" out="$2" key="$3" value="$4"
    if grep -q "^ *${key}:" "$in"; then
        sed "s|^ *${key}:.*|${key}: ${value}|" "$in" >"$out"
    else
        { cat "$in"; echo "${key}: ${value}"; } >"$out"
    fi
}

# max_rss <dir> <cfg>: runs the solver in <dir> and sets rc and rss (bytes).
# Sets skip to a reason when time(1) cannot measure here.
max_rss() {
    local d="$1" cfg="$2" time_opt
    skip=""; rss=""; rc=0
    if [[ "$is_windows" == 1 ]]; then
        skip="time(1) cannot measure native Windows processes"; return
    fi
    # Peak memory from time(1): BSD time -l reports bytes, GNU time -v kilobytes.
    case "$(uname -s)" in
        Darwin) time_opt=-l ;;
        Linux)  time_opt=-v ;;
        *)      skip="no known time(1) for $(uname -s)"; return ;;
    esac
    if [[ ! -x /usr/bin/time ]]; then skip="/usr/bin/time not installed"; return; fi
    mkdir -p "$d"
    local log="$d/time.log"
    ( cd "$d" && /usr/bin/time "$time_opt" "$BIN" "$cfg" </dev/null >"$d/out.log" 2>"$log" ) || rc=$?
    if [[ "$time_opt" == -l ]]; then
        rss="$(sed -n 's/^ *\([0-9][0-9]*\)  *maximum resident set size.*/\1/p' "$log" | head -n1)"
    else
        rss="$(sed -n 's/^.*Maximum resident set size (kbytes): *\([0-9][0-9]*\).*/\1/p' "$log" | head -n1)"
        [[ -z "$rss" ]] || rss=$((rss * 1024))
    fi
}

test_memory_bounded() {
    local name="${FUNCNAME[0]}" d="$work_dir/mem"
    mkdir -p "$d"
    local cfg="$d/config.yaml"
    sed 's/^\( *EnablePlotting:\).*/\1 true/' "$DEFAULT_CFG" >"$d/plot.yaml"
    set_key "$d/plot.yaml" "$cfg" VideoTimePerSecond 1.0e-4   # about 12000 frames
    max_rss "$d" "$cfg"
    if [[ -n "$skip" ]]; then echo "SKIP $name ($skip)"; return; fi
    if [[ "$rc" -ne 0 ]]; then fail "$name" "solver exited with status $rc"; return; fi
    if ! grep -q 'Video written to' "$d/out.log"; then fail "$name" "no 'Video written to' in output"; return; fi
    if [[ -z "$rss" ]]; then fail "$name" "no max RSS in time output"; return; fi
    if (( rss < 1073741824 )); then pass "$name (max RSS $rss bytes)"
    else fail "$name" "max RSS $rss bytes >= 1 GiB"; fi
}

# Without plotting nothing is stored per step: a long run stays small.
test_memory_without_plotting() {
    local name="${FUNCNAME[0]}" d="$work_dir/memnp"
    mkdir -p "$d"
    sed -e 's/^\( *EnablePlotting:\).*/\1 false/' -e 's/^N:.*/N: 400/' \
        -e 's/^order:.*/order: 3/' -e 's/^t1:.*/t1: 1.0e-2/' "$DEFAULT_CFG" >"$d/config.yaml"
    max_rss "$d" "$d/config.yaml"
    if [[ -n "$skip" ]]; then echo "SKIP $name ($skip)"; return; fi
    if [[ "$rc" -ne 0 ]]; then fail "$name" "solver exited with status $rc"; return; fi
    if ! grep -q 'The calculations are finished!' "$d/out.log"; then fail "$name" "run did not finish"; return; fi
    if [[ -z "$rss" ]]; then fail "$name" "no max RSS in time output"; return; fi
    if (( rss < 134217728 )); then pass "$name (max RSS $rss bytes)"
    else fail "$name" "max RSS $rss bytes >= 128 MiB"; fi
}

# The memory check must come before anything the size of the grid is
# allocated: 2e8 cells need 1.6 GB for the grid points alone, while the video
# (about 100 000 frames x 2e8 values, ~160 TB) is rejected at once.
test_memory_check_before_grid() {
    local name="${FUNCNAME[0]}" d="$work_dir/memgrid"
    mkdir -p "$d"
    sed -e 's/^\( *EnablePlotting:\).*/\1 true/' -e 's/^N:.*/N: 200000000/' \
        -e 's/^order:.*/order: 1/' "$DEFAULT_CFG" >"$d/plot.yaml"
    set_key "$d/plot.yaml" "$d/config.yaml" VideoTimePerSecond 1.2e-5
    max_rss "$d" "$d/config.yaml"
    if [[ -n "$skip" ]]; then echo "SKIP $name ($skip)"; return; fi
    if [[ "$rc" -ne 1 ]]; then fail "$name" "solver exited with status $rc, expected 1"; return; fi
    # The solver's stderr shares time.log with the time(1) report.
    if ! grep -q 'Config error: the video needs' "$d/out.log" "$d/time.log"; then
        fail "$name" "no 'Config error: the video needs' in output"; return; fi
    if [[ -z "$rss" ]]; then fail "$name" "no max RSS in time output"; return; fi
    if (( rss < 134217728 )); then pass "$name (max RSS $rss bytes)"
    else fail "$name" "max RSS $rss bytes >= 128 MiB"; fi
}

# A buffer that passes the memory check can still fail to allocate (strict
# overcommit, other processes, address-space limits). A 1 GB virtual memory
# limit forces that for a 2 GB buffer (about 100 000 frames x 2500 values);
# the run must end with a message instead of an uncaught std::bad_alloc.
# Linux only: macOS does not enforce ulimit -v, and under QEMU user emulation
# (x64 images on Apple Silicon) the limit is accepted but never applied.
test_buffer_allocation_failure_reported() {
    local name="${FUNCNAME[0]}" d="$work_dir/allocfail"
    if [[ "$(uname -s)" != Linux ]]; then
        echo "SKIP $name (ulimit -v is enforced only on Linux)"; return; fi
    if [[ "$(ulimit -v 1000000 && ulimit -v)" != 1000000 ]]; then
        echo "SKIP $name (ulimit -v has no effect here, e.g. under QEMU)"; return; fi
    local ram_kb; ram_kb="$(sed -n 's/^MemTotal: *\([0-9]*\) kB/\1/p' /proc/meminfo)"
    if (( ram_kb < 3500000 )); then
        echo "SKIP $name (under 3.5 GB RAM the memory check rejects the 2 GB video first)"; return; fi
    mkdir -p "$d"
    sed -e 's/^\( *EnablePlotting:\).*/\1 true/' -e 's/^N:.*/N: 500/' "$DEFAULT_CFG" >"$d/plot.yaml"
    set_key "$d/plot.yaml" "$d/config.yaml" VideoTimePerSecond 1.2e-5
    local out rc=0
    out="$(cd "$d" && ulimit -v 1000000 && timeout 60 "$BIN" config.yaml </dev/null 2>&1)" || rc=$?
    if [[ "$rc" != 1 ]]; then fail "$name" "exit code $rc, expected 1"; return; fi
    [[ "$out" == *'the video buffer of '*' could not be allocated'* ]] || {
        fail "$name" "missing 'the video buffer of ... could not be allocated'"; return; }
    pass "$name"
}

# expect_frames <name> <expected frames> <key> <value> [<key> <value> ...]:
# renders the smoke config with the given keys set; the announced frame count
# must be the expected one and the video must hold exactly that many frames.
expect_frames() {
    local name="$1" expected="$2"; shift 2
    local d="$work_dir/$name" cfg="$work_dir/$name.yaml" tmp="$work_dir/$name.tmp"
    mkdir -p "$d"
    cp "$SMOKE_CFG" "$cfg"
    while [[ $# -gt 0 ]]; do
        set_key "$cfg" "$tmp" "$1" "$2"; mv "$tmp" "$cfg"; shift 2
    done
    cd "$d"
    local rc=0 out; out="$(timeout 120 "$BIN" "$cfg" </dev/null 2>&1 | tr -d '\r')" || rc=$?
    local n; n="$(sed -n 's/^Rendering \([0-9][0-9]*\) frames at 60 fps$/\1/p' <<<"$out" | head -n1)"
    local probe=""
    if [[ -s output.mp4 ]]; then
        probe="$(ffprobe -v error -count_frames -select_streams v:0 \
            -show_entries stream=nb_read_frames -of csv=p=0 output.mp4)" || probe="ffprobe failed"
    fi
    cd "$work_dir"
    if [[ "$rc" -ne 0 ]]; then fail "$name" "solver exited with status $rc"; return; fi
    if [[ "$n" != "$expected" ]]; then fail "$name" "rendering $n frames, expected $expected"; return; fi
    if [[ "$probe" != "$expected" ]]; then fail "$name" "ffprobe counted '$probe' frames, expected $expected"; return; fi
    pass "$name"
}

# 8e-5 s * 60 / 1e-3 s = 4.8, so 5 frames, however fine the grid is.
test_frame_count_independent_of_grid() {
    expect_frames "${FUNCNAME[0]}_N4" 5 N 4
    expect_frames "${FUNCNAME[0]}_N8" 5 N 8
}

# A frame spacing of 1e-5 / 60 s is far below the solver step (3.75e-5 s).
test_frames_between_steps() {
    expect_frames "${FUNCNAME[0]}" 481 VideoTimePerSecond 1.0e-5
}

test_single_frame_video() {
    expect_frames "${FUNCNAME[0]}" 1 VideoTimePerSecond 1.0
}

test_frame_count_matches
test_ffmpeg_failure_reported
test_frame_count_independent_of_grid
test_frames_between_steps
test_single_frame_video
test_memory_bounded
test_memory_without_plotting
test_memory_check_before_grid
test_buffer_allocation_failure_reported

if [[ "$failures" -ne 0 ]]; then
    echo "$failures video render test(s) failed."
    exit 1
fi
echo "All video render tests passed."
