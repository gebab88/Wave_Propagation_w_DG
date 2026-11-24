#!/usr/bin/env bash
# Drives the interactive time-integrator menu in a pseudo-terminal via script(1).
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

make Release >/dev/null

BIN="$repo_root/bin/Release/DiscontinousGalerkin"
CFG="$repo_root/tests/menu-test-config.yaml"

work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
cd "$work_dir"   # the solver may write files into the CWD

failures=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1: $2"; failures=$((failures + 1)); }

# run_tty <key1> [<key2> ...] -- <cmd...>
# Runs cmd in a pseudo-terminal; each key is sent separately, 0.5 s after the
# previous event (raw mode must be active first), then 1 s of grace.
run_tty() {
    local keys=()
    while [[ $# -gt 0 && "$1" != "--" ]]; do keys+=("$1"); shift; done
    shift  # drop "--"
    local limit="${TTY_TIMEOUT:-20}"
    if [[ "$(uname -s)" == "Darwin" ]]; then
        ( for k in ${keys[@]+"${keys[@]}"}; do sleep 0.5; printf '%s' "$k"; done; sleep 1 ) \
            | timeout "$limit" script -q /dev/null "$@" 2>&1 | tr -d '\r' || true
    else
        local cmd
        printf -v cmd '%q ' "$@"
        ( for k in ${keys[@]+"${keys[@]}"}; do sleep 0.5; printf '%s' "$k"; done; sleep 1 ) \
            | timeout "$limit" script -qec "$cmd" /dev/null 2>&1 | tr -d '\r' || true
    fi
}

expect_contains() {  # name output needle
    if [[ "$2" == *"$3"* ]]; then return 0; fi
    fail "$1" "missing '$3'"; return 1
}

test_key_selects_method() {
    local out; out="$(run_tty 2 -- "$BIN" "$CFG")"
    expect_contains "${FUNCNAME[0]}" "$out" 'Select time integration method:' &&
    expect_contains "${FUNCNAME[0]}" "$out" '  5) PredictorCorrectorHeun   (config.yaml)' &&
    expect_contains "${FUNCNAME[0]}" "$out" 'Using: RungeKuttaClassic' &&
    expect_contains "${FUNCNAME[0]}" "$out" 'The calculations are finished!' &&
    pass "${FUNCNAME[0]}"
}

test_enter_uses_config() {
    local out; out="$(run_tty $'\r' -- "$BIN" "$CFG")"
    expect_contains "${FUNCNAME[0]}" "$out" 'Using: PredictorCorrectorHeun' && pass "${FUNCNAME[0]}"
}

test_invalid_key_reprompts() {
    local out n; out="$(run_tty x 2 -- "$BIN" "$CFG")"
    n="$(grep -c 'Invalid key, press 1-5 or Enter.' <<<"$out" || true)"
    [[ "$n" == 1 ]] || { fail "${FUNCNAME[0]}" "expected 1 invalid message, got $n"; return; }
    expect_contains "${FUNCNAME[0]}" "$out" 'Using: RungeKuttaClassic' && pass "${FUNCNAME[0]}"
}

test_arrow_key_single_message() {
    local out n; out="$(run_tty $'\033[A' 2 -- "$BIN" "$CFG")"
    n="$(grep -c 'Invalid key, press 1-5 or Enter.' <<<"$out" || true)"
    [[ "$n" == 1 ]] || { fail "${FUNCNAME[0]}" "expected 1 invalid message, got $n"; return; }
    expect_contains "${FUNCNAME[0]}" "$out" 'Using: RungeKuttaClassic' && pass "${FUNCNAME[0]}"
}

test_ctrl_d_uses_config() {
    local out; out="$(run_tty $'\004' -- "$BIN" "$CFG")"
    expect_contains "${FUNCNAME[0]}" "$out" 'Using: PredictorCorrectorHeun' && pass "${FUNCNAME[0]}"
}

test_no_tty_skips_menu() {
    local out rc=0; out="$("$BIN" "$CFG" </dev/null 2>&1)" || rc=$?
    [[ $rc -eq 0 ]] || { fail "${FUNCNAME[0]}" "exit code $rc"; return; }
    [[ "$out" != *'Select time integration method'* ]] || { fail "${FUNCNAME[0]}" "menu was shown"; return; }
    expect_contains "${FUNCNAME[0]}" "$out" 'Using: PredictorCorrectorHeun' && pass "${FUNCNAME[0]}"
}

test_invalid_config_fails_early() {
    local bad="$work_dir/bad.yaml" rc=0 out err
    sed 's/^TimeIntegration:.*/TimeIntegration: Foo/' "$CFG" > "$bad"
    out="$("$BIN" "$bad" </dev/null 2>"$work_dir/err.txt")" || rc=$?
    err="$(cat "$work_dir/err.txt")"
    [[ $rc -eq 1 ]] || { fail "${FUNCNAME[0]}" "exit code $rc, expected 1"; return; }
    expect_contains "${FUNCNAME[0]}" "$err" "Config error: unknown TimeIntegration 'Foo'" || return
    [[ "$out" != *'Q='* ]] || { fail "${FUNCNAME[0]}" "setup output printed before validation"; return; }
    pass "${FUNCNAME[0]}"
}

test_prompt_flushed_when_stdout_piped() {
    local out; out="$(TTY_TIMEOUT=3 run_tty -- sh -c "'$BIN' '$CFG' | cat")"
    expect_contains "${FUNCNAME[0]}" "$out" 'Select time integration method:' &&
    expect_contains "${FUNCNAME[0]}" "$out" 'Press 1-5' &&
    pass "${FUNCNAME[0]}"
}

test_ctrl_c_restores_terminal() {
    local out; out="$(run_tty $'\003' -- sh -c "trap ':' INT; '$BIN' '$CFG'; echo status=\$?; stty -a")"
    expect_contains "${FUNCNAME[0]}" "$out" 'status=130' || return
    if grep -qE -- '(^|[[:space:]])-(icanon|echo)([[:space:];]|$)' <<<"$out"; then
        fail "${FUNCNAME[0]}" "terminal left in raw mode"; return
    fi
    grep -qE '(^|[[:space:]])icanon([[:space:];]|$)' <<<"$out" || { fail "${FUNCNAME[0]}" "no icanon in stty output"; return; }
    grep -qE '(^|[[:space:]])echo([[:space:];]|$)' <<<"$out" || { fail "${FUNCNAME[0]}" "no echo in stty output"; return; }
    pass "${FUNCNAME[0]}"
}

test_ignored_sigint_stays_ignored() {
    local out; out="$(run_tty $'\003' 2 -- sh -c "trap '' INT; '$BIN' '$CFG'; echo status=\$?")"
    expect_contains "${FUNCNAME[0]}" "$out" 'Using: RungeKuttaClassic' &&
    expect_contains "${FUNCNAME[0]}" "$out" 'status=0' &&
    pass "${FUNCNAME[0]}"
}

test_background_job_skips_menu() {
    local out; out="$(run_tty -- bash -c "set -m; '$BIN' '$CFG' & wait \$!; echo status=\$?")"
    [[ "$out" != *'Select time integration method'* ]] || { fail "${FUNCNAME[0]}" "menu was shown"; return; }
    expect_contains "${FUNCNAME[0]}" "$out" 'status=0' &&
    expect_contains "${FUNCNAME[0]}" "$out" 'Using: PredictorCorrectorHeun' &&
    pass "${FUNCNAME[0]}"
}

test_key_selects_method || true
test_enter_uses_config || true
test_invalid_key_reprompts || true
test_arrow_key_single_message || true
test_ctrl_d_uses_config || true
test_no_tty_skips_menu || true
test_invalid_config_fails_early || true
test_prompt_flushed_when_stdout_piped || true
test_ctrl_c_restores_terminal || true
test_ignored_sigint_stays_ignored || true
test_background_job_skips_menu || true

if [[ $failures -ne 0 ]]; then
    echo "$failures test(s) failed" >&2
    exit 1
fi
echo "All integrator menu tests passed."
