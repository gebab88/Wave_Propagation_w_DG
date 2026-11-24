#!/usr/bin/env bash
# Config values are checked at startup, and a run whose pressure leaves the
# physically possible range is stopped with an error instead of continuing.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

make Release >/dev/null

BIN="$repo_root/bin/Release/DiscontinousGalerkin"

work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
cd "$work_dir"

failures=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1: $2"; failures=$((failures + 1)); }

# write_cfg <file> [key=value ...]
# A small valid problem (well under a second) with the given keys replaced.
write_cfg() {
    local file="$1"; shift
    cat >"$file" <<'EOF'
TimeIntegration: RungeKuttaClassic
EnablePlotting: false
OpenMPMinCells: 50
OpenMPThreads: 2
OpenMPLargeMinCells: 512
OpenMPLargeThreads: 4
CFLReserve: 0.45
N: 4
order: 3
t0: 0.0
t1: 8.0e-5
x0: 0.0
x1: 0.5
c_0: 300.0
rho_0: 1.5
freq_i: 3000.0
mu: 0.1
sigma: 0.02
height: 1.0
EOF
    local kv key
    for kv in "$@"; do
        key="${kv%%=*}"
        grep -q "^${key}:" "$file" || { echo "test bug: unknown key '$key'" >&2; exit 2; }
        sed -i.bak "s|^${key}:.*|${key}: ${kv#*=}|" "$file"
    done
    rm -f "$file.bak"
}

# run_solver <cfg> <seconds>: runs in a fresh directory, sets rc, out, err and
# run_dir. A solver that does not terminate in time shows up as rc 124.
run_solver() {
    run_dir="$(mktemp -d "$work_dir/run.XXXXXX")"
    rc=0
    out="$(cd "$run_dir" && timeout "$2" "$BIN" "$1" </dev/null 2>"$run_dir/stderr.txt")" || rc=$?
    err="$(cat "$run_dir/stderr.txt")"
}

# expect_rejected <test name> <key the error must name> key=value ...
expect_rejected() {
    local name="$1" key="$2"; shift 2
    local cfg="$work_dir/$name.yaml"
    write_cfg "$cfg" "$@"
    run_solver "$cfg" 5
    if [[ $rc -eq 124 ]]; then fail "$name" "did not terminate within 5 s"; return; fi
    if [[ $rc -ne 1 ]]; then fail "$name" "exit code $rc, expected 1"; return; fi
    if [[ "$err" != *"Config error"*"$key"* ]]; then fail "$name" "no config error naming '$key': $err"; return; fi
    pass "$name"
}

test_valid_config_accepted() {
    local cfg="$work_dir/valid.yaml"
    write_cfg "$cfg"
    run_solver "$cfg" 30
    [[ $rc -eq 0 ]] || { fail "${FUNCNAME[0]}" "exit code $rc: $err"; return; }
    [[ "$out" == *'The calculations are finished!'* ]] || { fail "${FUNCNAME[0]}" "run did not finish"; return; }
    pass "${FUNCNAME[0]}"
}

# expect_stopped <test name> key=value ...: the run must end with status 1, an
# error that points at CFLReserve, and no video.
expect_stopped() {
    local name="$1"; shift
    local cfg="$work_dir/$name.yaml"
    write_cfg "$cfg" N=50 order=5 t1=2.0e-3 EnablePlotting=true "$@"
    run_solver "$cfg" 60
    if [[ $rc -ne 1 ]]; then fail "$name" "exit code $rc, expected 1"; return; fi
    if [[ "$err" != *CFLReserve* ]]; then fail "$name" "error does not mention CFLReserve: $err"; return; fi
    if [[ -e "$run_dir/output.mp4" ]]; then fail "$name" "a video was written"; return; fi
    pass "$name"
}

# expect_completes <test name> key=value ...: a correct run must not be stopped.
expect_completes() {
    local name="$1"; shift
    local cfg="$work_dir/$name.yaml"
    write_cfg "$cfg" N=50 t1=2.0e-2 "$@"
    run_solver "$cfg" 120
    if [[ $rc -ne 0 ]]; then fail "$name" "exit code $rc: $err"; return; fi
    if [[ "$out" != *'The calculations are finished!'* ]]; then fail "$name" "run did not finish"; return; fi
    pass "$name"
}

test_valid_config_accepted

# A non-positive time step (and an endless time loop) without these checks:
expect_rejected test_swapped_domain_rejected        x1 x0=0.5 x1=0.0
expect_rejected test_empty_domain_rejected          x1 x0=0.5 x1=0.5
expect_rejected test_zero_cfl_rejected              CFLReserve CFLReserve=0.0
expect_rejected test_negative_cfl_rejected          CFLReserve CFLReserve=-0.1
expect_rejected test_infinite_sound_speed_rejected  c_0 c_0=.inf
# Meaningless physics (division by zero, swapped characteristics):
expect_rejected test_zero_sound_speed_rejected      c_0 c_0=0.0
expect_rejected test_negative_sound_speed_rejected  c_0 c_0=-300.0
expect_rejected test_zero_density_rejected          rho_0 rho_0=0.0
# Time interval and non-finite values:
expect_rejected test_reversed_time_rejected         t1 t0=1.0e-4 t1=0.0
expect_rejected test_empty_time_interval_rejected   t1 t1=0.0
expect_rejected test_infinite_end_time_rejected     t1 t1=.inf
expect_rejected test_infinite_domain_rejected       x1 x1=.inf
expect_rejected test_nan_cfl_rejected               CFLReserve CFLReserve=.nan
expect_rejected test_nan_frequency_rejected         freq_i freq_i=.nan

# Euler at the default CFLReserve: |p| passes 1e30 within 2 ms but stays finite.
expect_stopped test_unstable_euler_stopped          TimeIntegration=EulerExplicit
# RK4 at 0.95 lies below the eigenvalue limit (1.02), yet disturbances at the
# inflow grow to |p| = 600 and stay there; nothing turns inf or NaN.
expect_stopped test_transient_growth_stopped        CFLReserve=0.95

# Correct runs: default problem (max |p| = 1.9996) and order 3, which overshoots
# the exact maximum 2 slightly (max |p| = 2.004).
expect_completes test_default_problem_completes     order=5
expect_completes test_order3_overshoot_completes    order=3

if [[ $failures -ne 0 ]]; then
    echo "$failures input check test(s) failed." >&2
    exit 1
fi
echo "All input check tests passed."
