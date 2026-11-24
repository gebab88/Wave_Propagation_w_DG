#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

skip_build=0
if [[ "${1:-}" == "--skip-build" ]]; then
    skip_build=1
fi

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "Smoke test must run on Linux, got $(uname -s)" >&2
    exit 1
fi

# Dockerfile.linux sets EXPECTED_ARCH to the image platform (amd64 or arm64),
# so an image built for the wrong platform fails here instead of silently
# testing the host architecture. Without it, any architecture is accepted.
machine="$(uname -m)"
case "${EXPECTED_ARCH:-}" in
    "")    true ;;
    amd64) [[ "$machine" == x86_64 ]] ;;
    arm64) [[ "$machine" == aarch64 || "$machine" == arm64 ]] ;;
    *)     false ;;
esac || { echo "Smoke test expected architecture '$EXPECTED_ARCH', got $machine" >&2; exit 1; }

if [[ "$skip_build" -eq 0 ]]; then
    make cleanOBJ cleanBIN
    make Release
fi

run_dir="$(mktemp -d)"
trap 'rm -rf "$run_dir"' EXIT

cd "$run_dir"
"$repo_root/bin/Release/DiscontinousGalerkin" "$repo_root/tests/linux-smoke-config.yaml" </dev/null | tee run.log

if [[ ! -s output.mp4 ]]; then
    echo "Expected non-empty output.mp4" >&2
    exit 1
fi

frame_count="$(ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of csv=p=0 output.mp4)"
expected_frames="$(sed -n 's/^Rendering \([0-9][0-9]*\) frames at 60 fps$/\1/p' run.log | head -n1)"
if [[ -z "$expected_frames" ]]; then
    echo "No 'Rendering <n> frames at 60 fps' line in program output" >&2
    exit 1
fi
if ! [[ "$frame_count" =~ ^[0-9]+$ && "$frame_count" == "$expected_frames" ]]; then
    echo "Expected $expected_frames video frames, got '$frame_count'" >&2
    exit 1
fi

file "$repo_root/bin/Release/DiscontinousGalerkin"
file output.mp4
echo "Linux $machine smoke test passed with $frame_count frames."
