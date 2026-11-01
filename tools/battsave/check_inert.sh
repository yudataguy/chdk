#!/usr/bin/env bash
# Verify a built port without CAM_HAS_BATTSAVE contains no Battery Saver code.
# Usage: tools/battsave/check_inert.sh PLATFORM SUB   (after tools/battsave/build.sh)
set -euo pipefail
cd "$(dirname "$0")/../.."
elf="platform/$1/sub/$2/main.elf"
[ -f "$elf" ] || { echo "missing $elf (build first)" >&2; exit 2; }
syms=$(docker run --rm --platform linux/amd64 -v "$PWD":/srv/src chdkbuild \
  arm-none-eabi-nm "$elf" | grep -i battsave || true)
if [ -n "$syms" ]; then
  echo "FAIL: battsave symbols linked into $1 $2:"; echo "$syms"; exit 1
fi
echo "OK: no battsave symbols in $1 $2"
