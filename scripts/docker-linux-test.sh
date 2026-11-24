#!/usr/bin/env bash
# Builds the Linux test image (Dockerfile.linux) for one architecture and one
# Ubuntu release (default 24.04). Building it compiles the solver and runs the
# tests. On Apple Silicon, x64 runs emulated (QEMU via binfmt, or Rosetta).
#   scripts/docker-linux-test.sh arm64|x64 [ubuntu-version]
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

case "${1:-}" in
    arm64) platform=linux/arm64; arch=arm64; base_repo=arm64v8/ubuntu ;;
    x64)   platform=linux/amd64; arch=amd64; base_repo=amd64/ubuntu ;;
    *)     echo "usage: $0 arm64|x64 [ubuntu-version]" >&2; exit 2 ;;
esac
ubuntu="${2:-24.04}"
image="${IMAGE:-dg-linux-$1-test:$ubuntu}"

# The base comes from the per-architecture repository, which holds only this
# platform (see the note in Dockerfile.linux). EXPECTED_ARCH lets the smoke
# test in the image verify the platform; the legacy builder does not set
# TARGETARCH, so it is passed explicitly.
docker build \
    --platform "$platform" \
    --build-arg BASE_IMAGE="$base_repo:$ubuntu" \
    --build-arg EXPECTED_ARCH="$arch" \
    -f Dockerfile.linux \
    -t "$image" \
    .

echo "Docker Linux $1 test image (Ubuntu $ubuntu) built successfully: $image"
