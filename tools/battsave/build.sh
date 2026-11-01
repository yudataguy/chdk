#!/usr/bin/env bash
# Build CHDK for any port in the project's Docker toolchain image.
# Usage: tools/battsave/build.sh PLATFORM SUB [make target]   e.g. sx50hs 100c
# Output: bin/*.zip (default target firzipsub)
set -euo pipefail

cd "$(dirname "$0")/../.."
[ $# -ge 2 ] || { echo "usage: $0 PLATFORM SUB [target]" >&2; exit 2; }
platform="$1"; sub="$2"; target="${3:-firzipsub}"

image=chdkbuild
platform_flag=(--platform linux/amd64)
if ! docker image inspect "$image" >/dev/null 2>&1; then
  docker build "${platform_flag[@]}" -t "$image" tools/docker/chdkbuild
fi
uid_flag=()
if [ "$(uname)" = "Linux" ]; then
  uid_flag=(--user "$(id -u):$(id -g)")
fi

mkdir -p bin   # git does not track the empty bin/ dir the CHDK makefiles expect

echo "=== $platform $sub"
docker run --rm "${platform_flag[@]}" "${uid_flag[@]}" -v "$PWD":/srv/src "$image" \
  make PLATFORM="$platform" PLATFORMSUB="$sub" "$target"
