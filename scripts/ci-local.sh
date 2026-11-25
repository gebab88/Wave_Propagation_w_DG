#!/usr/bin/env bash
# Runs the CI's Linux jobs locally in Docker and, on macOS, the same steps on
# the host, then prints a summary like the CI. Windows runs only in GitHub
# Actions. On Apple Silicon, linux-arm64 runs natively and linux-x64 emulated.
#   scripts/ci-local.sh [mac] [linux-arm64] [linux-x64]   (default: all)
# UBUNTU=<version> selects the Ubuntu release of the images (default 24.04,
# as in the CI). Each job runs `make Release Debug` and `make test`; the Linux
# jobs set CI=true like GitHub Actions, so menu tests without a terminal fail
# instead of being skipped. Logs go to bin/ci-local/. If Docker is not
# reachable and colima is installed, the script starts colima and stops it
# again at the end.
set -uo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

ubuntu="${UBUNTU:-24.04}"
log_dir="$repo_root/bin/ci-local"
mkdir -p "$log_dir"

all_jobs=(linux-arm64 linux-x64)
[[ "$(uname -s)" == Darwin ]] && all_jobs=(mac "${all_jobs[@]}")
selected=("$@")
[[ ${#selected[@]} -eq 0 ]] && selected=("${all_jobs[@]}")
for job in "${selected[@]}"; do
    case "$job" in
        mac) [[ "$(uname -s)" == Darwin ]] || { echo "the mac job needs macOS" >&2; exit 2; } ;;
        linux-arm64|linux-x64) ;;
        *) echo "unknown job '$job' (known: ${all_jobs[*]})" >&2; exit 2 ;;
    esac
done

started_colima=0
stop_colima() {
    if [[ "$started_colima" == 1 ]]; then
        echo "Stopping colima (started by this script) ..."
        colima stop >/dev/null 2>&1 || echo "colima stop failed" >&2
    fi
}
trap stop_colima EXIT

if [[ " ${selected[*]} " == *" linux-"* ]] && ! docker info >/dev/null 2>&1; then
    if command -v colima >/dev/null 2>&1; then
        echo "Docker is not reachable: starting colima ..."
        colima start >/dev/null 2>&1 || { echo "colima start failed" >&2; exit 1; }
        started_colima=1
    else
        echo "Docker is not reachable and colima is not installed" >&2
        exit 1
    fi
fi

# run_job <job>: runs one job, logging to bin/ci-local/<job>.log (and the image
# build to <job>-build.log); sets job_rc.
run_job() {
    local job="$1" log="$log_dir/$1.log"
    job_rc=0
    case "$job" in
        mac)
            { make Release Debug && make test; } >"$log" 2>&1 || job_rc=$?
            ;;
        linux-*)
            local arch="${job#linux-}" platform build_log="$log_dir/$job-build.log"
            [[ "$arch" == arm64 ]] && platform=linux/arm64 || platform=linux/amd64
            # g++ under QEMU crashes now and then (README); one retry of the
            # image build covers that.
            if ! scripts/docker-linux-test.sh "$arch" "$ubuntu" >"$build_log" 2>&1; then
                echo "  image build failed, retrying once" | tee -a "$build_log"
                scripts/docker-linux-test.sh "$arch" "$ubuntu" >>"$build_log" 2>&1 || {
                    job_rc=1; echo "image build failed, see $build_log" >"$log"; return; }
            fi
            docker run --rm --platform "$platform" -e CI=true "dg-linux-$arch-test:$ubuntu" \
                bash -c 'make Release Debug && make test' >"$log" 2>&1 || job_rc=$?
            ;;
    esac
}

names=() results=() times=() counts=()
for job in "${selected[@]}"; do
    case "$job" in
        mac) name="macOS $(uname -m) (not in the CI)" ;;
        *)   name="Ubuntu $ubuntu ${job#linux-}" ;;
    esac
    echo "== $name ..."
    start=$SECONDS
    run_job "$job"
    secs=$((SECONDS - start))
    log="$log_dir/$job.log"
    pass="$(grep -c '^PASS' "$log")"; fail="$(grep -c '^FAIL' "$log")"; skip="$(grep -c '^SKIP' "$log")"
    if [[ "$job_rc" == 0 ]]; then result=success; else result=failure; fi
    printf '   %s in %dm%02ds (%s PASS, %s FAIL, %s SKIP)\n' "$result" $((secs / 60)) $((secs % 60)) "$pass" "$fail" "$skip"
    [[ "$job_rc" == 0 ]] || grep -E '^FAIL|[Ee]rror' "$log" | head -n 10 | sed 's/^/     /'
    names+=("$name"); results+=("$result"); times+=("$((secs / 60))m$(printf '%02d' $((secs % 60)))s")
    counts+=("$pass/$fail/$skip")
done

echo
echo "Summary (PASS/FAIL/SKIP, logs in bin/ci-local/):"
failed=0
for i in "${!names[@]}"; do
    printf '  %-34s %-8s %7s  %s\n' "${names[$i]}" "${results[$i]}" "${times[$i]}" "${counts[$i]}"
    [[ "${results[$i]}" == success ]] || failed=1
done
echo "  Windows x64 and arm64 run only in GitHub Actions."
exit "$failed"
