#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The sp410-cups-driver contributors.
#
# compare-vendor.sh - black-box differential test: vendor filter vs ours.
#
# Feeds identical CUPS raster pages to the vendor's closed-source filter and
# to sp410-rastertotspl, decodes both TSPL streams and reports every
# difference in setup commands and in the printed dots.
#
#   tools/re/compare-vendor.sh \
#       --vendor-filter ./vendor-inspect/extracted/.../raster-tspl \
#       --vendor-ppd    ./vendor-inspect/extracted/.../SP410.ppd \
#       [--wrapper "qemu-x86_64 -L /usr/x86_64-linux-gnu"]   # run x86 blob on a Pi
#       [--options "Darkness=10"]  [--patterns "shipping-k1 gray rgb"] \
#       [--raster my-page.ras]     [--out compare-out]
#
# Vendor option names differ from ours; pass vendor-specific ones with
# --vendor-options (they are only given to the vendor filter).
#
# Results (all under --out): <pattern>.vendor.prn, <pattern>.ours.prn,
# <pattern>.vendor.txt / .ours.txt (command listings), <pattern>.diff.txt,
# and PBM renders in <pattern>.vendor/ and <pattern>.ours/.

set -euo pipefail
HERE=$(cd "$(dirname "$0")/../.." && pwd)

VENDOR_FILTER='' VENDOR_PPD='' WRAPPER='' OPTIONS='' VENDOR_OPTIONS='' RASTER=''
PATTERNS="shipping-k1 shipping-w8 rgb gray multi copies odd"
OUT=compare-out
OUR_FILTER="$HERE/build/sp410-rastertotspl"
MKRASTER="$HERE/build/mkraster"
OUR_PPD="$HERE/ppd/idprt-sp410.ppd"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --vendor-filter)  VENDOR_FILTER=$2; shift 2 ;;
    --vendor-ppd)     VENDOR_PPD=$2; shift 2 ;;
    --wrapper)        WRAPPER=$2; shift 2 ;;
    --options)        OPTIONS=$2; shift 2 ;;
    --vendor-options) VENDOR_OPTIONS=$2; shift 2 ;;
    --patterns)       PATTERNS=$2; shift 2 ;;
    --raster)         RASTER=$2; shift 2 ;;
    --out)            OUT=$2; shift 2 ;;
    --our-filter)     OUR_FILTER=$2; shift 2 ;;
    -h|--help)        sed -n '4,25p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

[[ -n "$VENDOR_FILTER" && -n "$VENDOR_PPD" ]] || { echo "need --vendor-filter and --vendor-ppd" >&2; exit 2; }
[[ -x "$OUR_FILTER" && -x "$MKRASTER" ]] || { echo "run 'make' first" >&2; exit 2; }
mkdir -p "$OUT"

declare -a JOBS=()
if [[ -n "$RASTER" ]]; then
  JOBS=("custom:$RASTER")
else
  for p in $PATTERNS; do
    "$MKRASTER" "$p" "$OUT/$p.ras" >/dev/null
    JOBS+=("$p:$OUT/$p.ras")
  done
fi

rc=0
for job in "${JOBS[@]}"; do
  name=${job%%:*} ras=${job#*:}
  # shellcheck disable=SC2086
  PPD="$VENDOR_PPD" $WRAPPER "$VENDOR_FILTER" 1 re-test "$name" 1 "$OPTIONS $VENDOR_OPTIONS" "$ras" \
      > "$OUT/$name.vendor.prn" 2> "$OUT/$name.vendor.log" || echo "[$name] vendor filter exited $?"
  PPD="$OUR_PPD" "$OUR_FILTER" 1 re-test "$name" 1 "$OPTIONS" "$ras" \
      > "$OUT/$name.ours.prn" 2> "$OUT/$name.ours.log" || echo "[$name] our filter exited $?"

  python3 "$HERE/tools/tspl_decode.py" "$OUT/$name.vendor.prn" --pbm-dir "$OUT/$name.vendor" > "$OUT/$name.vendor.txt" || true
  python3 "$HERE/tools/tspl_decode.py" "$OUT/$name.ours.prn"   --pbm-dir "$OUT/$name.ours"   > "$OUT/$name.ours.txt" || true

  if python3 "$HERE/tools/tspl_decode.py" "$OUT/$name.vendor.prn" --diff "$OUT/$name.ours.prn" > "$OUT/$name.diff.txt"; then
    echo "[$name] equivalent"
  else
    echo "[$name] DIFFERENT:"; sed 's/^/    /' "$OUT/$name.diff.txt"; rc=1
  fi
  # Byte sizes are interesting too: a much smaller vendor stream means compression.
  printf '    bytes: vendor %s, ours %s\n' "$(stat -c %s "$OUT/$name.vendor.prn")" "$(stat -c %s "$OUT/$name.ours.prn")"
done
exit $rc
