#!/usr/bin/env bash
# Build CHDK for the Canon SX50 HS. Usage: tools/battsave/build_sx50.sh [100b|100c|all]
set -euo pipefail
cd "$(dirname "$0")/../.."
case "${1:-all}" in
  all) subs=(100b 100c) ;;
  100b|100c) subs=("$1") ;;
  *) echo "usage: $0 [100b|100c|all]" >&2; exit 2 ;;
esac
for sub in "${subs[@]}"; do tools/battsave/build.sh sx50hs "$sub"; done
ls -l bin/*sx50hs*
