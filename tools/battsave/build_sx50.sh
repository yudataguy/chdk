#!/usr/bin/env bash
# Build CHDK for the Canon SX50 HS in the project's Docker toolchain image.
# Usage: tools/battsave/build_sx50.sh [100b|100c|all]   (default: all)
# Output: bin/*sx50hs*.zip
set -euo pipefail

cd "$(dirname "$0")/../.."
which_sub="${1:-all}"
case "$which_sub" in
  all) subs=(100b 100c) ;;
  100b|100c) subs=("$which_sub") ;;
  *) echo "usage: $0 [100b|100c|all]" >&2; exit 2 ;;
esac

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

for sub in "${subs[@]}"; do
  echo "=== sx50hs $sub"
  docker run --rm "${platform_flag[@]}" "${uid_flag[@]}" -v "$PWD":/srv/src "$image" \
    make PLATFORM=sx50hs PLATFORMSUB="$sub" firzipsub
done

ls -l bin/*sx50hs* 
