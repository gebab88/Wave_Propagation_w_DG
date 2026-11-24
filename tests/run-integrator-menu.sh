#!/usr/bin/env bash
# Drives the interactive time-integrator menu in a pseudo-terminal via script(1),
# on Windows in a pseudo console via tests/conpty_run.cpp.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

is_windows=0; case "$(uname -s)" in MINGW*|UCRT64*|CLANG64*|CLANGARM64*|MSYS*) is_windows=1 ;; esac

if [[ "$is_windows" == 1 ]]; then make Release conpty-run >/dev/null; else make Release >/dev/null; fi

BIN="$repo_root/bin/Release/DiscontinousGalerkin"
CFG="$repo_root/tests/menu-test-config.yaml"

work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
cd "$work_dir"   # the solver may write files into the CWD

CONPTY_RUN="$repo_root/bin/Release/conpty_run"
TTY_CMD_FILE="$work_dir/tty-cmd.sh"

failures=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1: $2"; failures=$((failures + 1)); }

# run_tty <key1> [<key2> ...] -- <cmd...>
# Runs cmd in a pseudo-terminal; each key is sent separately, 0.5 s after the
# previous event (raw mode must be active first), then 1 s of grace. A key
# @wait:<text> sends nothing; it waits until <text> is in the output.
# Env: TTY_KEY_DELAY (default 0.5) pause before each key, TTY_TAIL (default 1)
# seconds the input stays open after the last key, TTY_TIMEOUT (default 20).
# TTY_CLOSE_AFTER: after the last key, wait until this text is in the output;
# TTY_TAIL then counts from there. When the input closes, script sends ^D
# (conpty_run on Windows: Ctrl+Z, Enter); just before that, run_tty creates
# $TTY_CLOSED_MARKER.
# On Windows, cmd goes into $TTY_CMD_FILE for bash, so it needs no quoting for
# the Windows command line. With CONPTY_RUN_RAW=<file> conpty_run appends the
# unfiltered console output there; run_tty adds the test name before it.
TTY_LOG="$work_dir/tty-output.log"
TTY_CLOSED_MARKER="$work_dir/tty-input-closed"
wait_for_output() {  # text, at most $limit seconds
    local deadline=$((SECONDS + limit))
    until grep -qF -- "$1" "$TTY_LOG" || (( SECONDS >= deadline )); do sleep 0.1; done
}
run_tty() {
    local keys=()
    while [[ $# -gt 0 && "$1" != "--" ]]; do keys+=("$1"); shift; done
    shift  # drop "--"
    local limit="${TTY_TIMEOUT:-20}"
    local delay="${TTY_KEY_DELAY:-0.5}" tail_s="${TTY_TAIL:-1}"
    local -a script_cmd
    if [[ "$is_windows" == 1 ]]; then
        printf '%q ' "$@" >"$TTY_CMD_FILE"
        script_cmd=("$CONPTY_RUN" bash "$TTY_CMD_FILE")
        if [[ -n "${CONPTY_RUN_RAW:-}" ]]; then printf '\n==== %s: %s\n' "${FUNCNAME[1]:-main}" "$*" >>"$CONPTY_RUN_RAW"; fi
    elif [[ "$(uname -s)" == "Darwin" ]]; then
        script_cmd=(script -q /dev/null "$@")
    else
        local cmd
        printf -v cmd '%q ' "$@"
        script_cmd=(script -qec "$cmd" /dev/null)
    fi
    : >"$TTY_LOG"
    rm -f "$TTY_CLOSED_MARKER"
    (
        for k in ${keys[@]+"${keys[@]}"}; do
            if [[ "$k" == @wait:* ]]; then wait_for_output "${k#@wait:}"; continue; fi
            sleep "$delay"; printf '%s' "$k"
        done
        if [[ -n "${TTY_CLOSE_AFTER:-}" ]]; then wait_for_output "$TTY_CLOSE_AFTER"; fi
        sleep "$tail_s"
        : >"$TTY_CLOSED_MARKER"
    ) | timeout "$limit" "${script_cmd[@]}" 2>&1 | tee "$TTY_LOG" | tr -d '\r' || true
}

show_output() {  # output
    echo "  first 40 lines of the output:"
    head -n 40 <<<"$1" | sed 's/^/  | /'
}

expect_contains() {  # name output needle
    if [[ "$2" == *"$3"* ]]; then return 0; fi
    fail "$1" "missing '$3'"
    show_output "$2"
    return 1
}

test_key_selects_method() {
    local out; out="$(run_tty 2 -- "$BIN" "$CFG")"
    expect_contains "${FUNCNAME[0]}" "$out" 'Select time integration method:' &&
    expect_contains "${FUNCNAME[0]}" "$out" 'PredictorCorrectorHeun  RK4 + iterated trapezoid, slowest   (config.yaml)' &&
    expect_contains "${FUNCNAME[0]}" "$out" 'Using: RungeKuttaClassic' &&
    expect_contains "${FUNCNAME[0]}" "$out" 'The calculations are finished!' &&
    pass "${FUNCNAME[0]}"
}

test_menu_shows_descriptions() {
    local out line; out="$(run_tty 2 -- "$BIN" "$CFG")"
    local expected=(
        '  1) EulerExplicit           Euler, 1st order, needs small CFL'
        '  2) RungeKuttaClassic       classic RK4, 4th order'
        '  3) RungeKuttaSecond        Kutta 3/8 rule, 4th order'
        '  4) RungeKuttaSecondOrder   Heun, 2nd order, cheapest at default CFL'
        '  5) PredictorCorrectorHeun  RK4 + iterated trapezoid, slowest   (config.yaml)'
    )
    for line in "${expected[@]}"; do
        if ! printf '%s\n' "$out" | grep -Fxq -- "$line"; then
            fail "${FUNCNAME[0]}" "missing exact line '$line'"; return 1
        fi
    done
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

test_function_key_single_message() {
    local out n; out="$(run_tty $'\033OP' 2 -- "$BIN" "$CFG")"
    n="$(grep -c 'Invalid key, press 1-5 or Enter.' <<<"$out" || true)"
    [[ "$n" == 1 ]] || { fail "${FUNCNAME[0]}" "expected 1 invalid message, got $n"; return; }
    expect_contains "${FUNCNAME[0]}" "$out" 'Using: RungeKuttaClassic' && pass "${FUNCNAME[0]}"
}

test_ctrl_d_uses_config() {
    local out; out="$(run_tty $'\004' -- "$BIN" "$CFG")"
    expect_contains "${FUNCNAME[0]}" "$out" 'Using: PredictorCorrectorHeun' && pass "${FUNCNAME[0]}"
}

# Ctrl+Z ends the input of a Windows console program. A wrongly mapped Ctrl+Z
# would still end at the config value (the Enter that conpty_run types at the
# end of the input), so the test also requires that no key was rejected.
test_ctrl_z_uses_config() {
    local out n; out="$(run_tty $'\032' -- "$BIN" "$CFG")"
    n="$(grep -c 'Invalid key, press 1-5 or Enter.' <<<"$out" || true)"
    [[ "$n" == 0 ]] || { fail "${FUNCNAME[0]}" "expected no invalid message, got $n"; return; }
    expect_contains "${FUNCNAME[0]}" "$out" 'Using: PredictorCorrectorHeun' && pass "${FUNCNAME[0]}"
}

test_no_tty_skips_menu() {
    local out rc=0; out="$("$BIN" "$CFG" </dev/null 2>&1)" || rc=$?
    [[ $rc -eq 0 ]] || { fail "${FUNCNAME[0]}" "exit code $rc"; return; }
    [[ "$out" != *'Select time integration method'* ]] || { fail "${FUNCNAME[0]}" "menu was shown"; return; }
    expect_contains "${FUNCNAME[0]}" "$out" 'Using: PredictorCorrectorHeun' && pass "${FUNCNAME[0]}"
}

# stdin is /dev/null inside a terminal session. On Windows, _isatty() is true
# for NUL as well; only a console handle has a console mode.
test_null_stdin_in_terminal_skips_menu() {
    local out; out="$(run_tty -- sh -c "'$BIN' '$CFG' </dev/null")"
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

test_config_error_before_menu() {
    local bad="$work_dir/bad-n.yaml" out
    sed 's/^N:.*/N: 1/' "$CFG" > "$bad"
    # Enter answers the menu if one appears, so the error is printed either way.
    out="$(run_tty $'\r' -- "$BIN" "$bad")"
    expect_contains "${FUNCNAME[0]}" "$out" 'Config error: N must be >= 2' || return
    [[ "$out" != *'Select time integration method'* ]] || { fail "${FUNCNAME[0]}" "menu shown before the config error"; return; }
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

# While _getch() waits, the console input mode is 0, so Ctrl+C arrives as the
# key 3 and the console raises no CTRL_C_EVENT. The menu must end the program
# itself instead of rejecting the key. The key goes out once the prompt is shown,
# so it cannot reach the console before _getch() runs. No trap in sh: a
# CTRL_C_EVENT would end sh as well, and the missing status line would show it.
# The MSYS2 runtime maps the exit code STATUS_CONTROL_C_EXIT to SIGINT, so sh
# sees 130, as on POSIX.
test_ctrl_c_ends_windows_menu() {
    local out status
    out="$(run_tty @wait:'Press 1-5' $'\003' -- sh -c "'$BIN' '$CFG'; echo status=\$?")"
    if [[ "$out" == *'Invalid key'* ]]; then
        fail "${FUNCNAME[0]}" "Ctrl+C was rejected as an invalid key"; show_output "$out"; return
    fi
    if [[ "$out" == *'Using:'* ]]; then
        fail "${FUNCNAME[0]}" "the program went on after Ctrl+C"; show_output "$out"; return
    fi
    status="$(grep -o 'status=[0-9]*' <<<"$out" | head -n 1)"
    [[ -n "$status" ]] || { fail "${FUNCNAME[0]}" "no status line"; show_output "$out"; return; }
    [[ "$status" == status=130 ]] || { fail "${FUNCNAME[0]}" "$status after Ctrl+C, expected status=130"; return; }
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

test_menu_time_not_in_elapsed() {
    local out t; out="$(TTY_KEY_DELAY=2 run_tty 2 -- "$BIN" "$CFG")"
    t="$(grep -m1 'elapsed time:' <<<"$out" | sed 's/.*elapsed time: \([0-9.e+-]*\)s.*/\1/')"
    [[ -n "$t" ]] || { fail "${FUNCNAME[0]}" "no elapsed time line"; return; }
    awk -v t="$t" 'BEGIN{exit !(t < 1)}' || { fail "${FUNCNAME[0]}" "elapsed time $t s includes menu wait"; return; }
    pass "${FUNCNAME[0]}"
}

test_enter_after_key_does_not_skip_exit_prompt() {
    local plot_cfg="$repo_root/tests/menu-plot-test-config.yaml" out d
    d="$(mktemp -d)"
    # '2' at 0.3 s and a habitual Enter 0.3 s after 'Using:', while the solver
    # still runs. Waiting for 'Using:' matters where startup is slow (Windows:
    # with fixed times the Enter can reach the console before the solver runs).
    # The input stays open until 2 s after the exit prompt has appeared; closing
    # it makes script send ^D (conpty_run: Ctrl+Z, Enter), which ends the final
    # cin.get(). Had the queued Enter answered the prompt, the program would have
    # ended within those 2 s, before the marker exists. Waiting for the prompt
    # instead of a fixed time matters when plotting is slow (about 23 s for x64
    # under QEMU).
    out="$(cd "$d" && TTY_KEY_DELAY=0.3 TTY_CLOSE_AFTER='Press enter to exit.' TTY_TAIL=2 TTY_TIMEOUT=120 \
        run_tty 2 @wait:Using: $'\r' -- bash -c "'$BIN' '$plot_cfg'; if [ -e '$TTY_CLOSED_MARKER' ]; then echo exit=after_input_closed; else echo exit=before_input_closed; fi")"
    rm -rf "$d"
    expect_contains "${FUNCNAME[0]}" "$out" 'Press enter to exit.' || return
    case "$out" in
        *exit=after_input_closed*)  pass "${FUNCNAME[0]}" ;;
        *exit=before_input_closed*) fail "${FUNCNAME[0]}" "exited before the input closed: the queued Enter answered the exit prompt" ;;
        *)                          fail "${FUNCNAME[0]}" "no exit line: the program still waited at the prompt when the timeout hit" ;;
    esac
}

test_menu_visible_when_stdout_redirected() {
    local log="$work_dir/redirect.log" out; rm -f "$log"
    out="$(run_tty 2 -- sh -c "'$BIN' '$CFG' > '$log'")"
    expect_contains "${FUNCNAME[0]}" "$out" 'Select time integration method:' || return
    grep -q 'Using: RungeKuttaClassic' "$log" 2>/dev/null || { fail "${FUNCNAME[0]}" "log lacks 'Using: RungeKuttaClassic'"; return; }
    pass "${FUNCNAME[0]}"
}

# All tests but test_no_tty_skips_menu and test_invalid_config_fails_early need
# a working terminal from script(1) (conpty_run on Windows). If MSYS2's bash
# does not see the pseudo console as a terminal, the solver itself (a native
# Windows program) decides. Under QEMU 7 user emulation, Ubuntu 26.04
# (glibc 2.43) gets none, e.g. in the x64 Docker image on Apple Silicon: its
# tcgetattr() uses the TCGETS2 ioctl, which QEMU 7 does not implement. Without
# a terminal those tests would fail, or pass for the wrong reason because the
# program silently falls back to the config value, so they are skipped. In CI
# (CI is set) they fail instead: there a missing terminal means the test setup
# is broken, and that must not pass as a skip.
pty_ok=0
if [[ "$(run_tty -- sh -c 'test -t 0 && echo pty-ok')" == *pty-ok* ]]; then
    pty_ok=1
elif [[ "$is_windows" == 1 && "$(run_tty $'\r' -- "$BIN" "$CFG")" == *'Select time integration method:'* ]]; then
    pty_ok=1
fi
skipped=0
pty_test() {  # test function
    if (( pty_ok )); then
        "$1" || true
    elif [[ -n "${CI:-}" ]]; then
        fail "$1" "script provides no working terminal (an error in CI, not a skip)"
    else
        echo "SKIP $1 (script provides no working terminal here)"
        skipped=$((skipped + 1))
    fi
}
posix_only() {  # test function, reason why it does not apply on Windows
    if [[ "$is_windows" == 1 ]]; then
        echo "SKIP $1 ($2)"; skipped=$((skipped + 1))
    else
        pty_test "$1"
    fi
}
windows_only() {  # test function, reason why it does not apply on POSIX
    if [[ "$is_windows" == 1 ]]; then
        pty_test "$1"
    else
        echo "SKIP $1 ($2)"; skipped=$((skipped + 1))
    fi
}

pty_test test_key_selects_method
pty_test test_menu_shows_descriptions
pty_test test_enter_uses_config
pty_test test_invalid_key_reprompts
pty_test test_arrow_key_single_message
pty_test test_function_key_single_message
pty_test test_ctrl_d_uses_config
windows_only test_ctrl_z_uses_config "Ctrl+Z suspends the job on POSIX"
test_no_tty_skips_menu || true
pty_test test_null_stdin_in_terminal_skips_menu
test_invalid_config_fails_early || true
pty_test test_config_error_before_menu
pty_test test_prompt_flushed_when_stdout_piped
posix_only test_ctrl_c_restores_terminal "termios: nothing to restore on Windows"
windows_only test_ctrl_c_ends_windows_menu "POSIX: see test_ctrl_c_restores_terminal"
posix_only test_ignored_sigint_stays_ignored "POSIX signal disposition"
posix_only test_background_job_skips_menu "no job control on Windows"
pty_test test_menu_time_not_in_elapsed
pty_test test_enter_after_key_does_not_skip_exit_prompt
pty_test test_menu_visible_when_stdout_redirected

if [[ $failures -ne 0 ]]; then
    echo "$failures test(s) failed" >&2
    exit 1
fi
echo "All integrator menu tests passed ($skipped skipped)."
