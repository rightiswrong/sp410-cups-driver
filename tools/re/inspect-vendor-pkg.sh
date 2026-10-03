#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The sp410-cups-driver contributors.
#
# inspect-vendor-pkg.sh - static inventory of a vendor Linux/RPi driver package.
#
#   tools/re/inspect-vendor-pkg.sh <package.zip|.deb|.tar.gz|dir> [outdir]
#
# Unpacks the package (recursively: zip -> deb -> data.tar), then writes
# <outdir>/report.md with:
#   * every file, its size, sha256 and `file` type
#   * Debian control data and maintainer scripts (where files are installed,
#     which lpadmin/udev/systemctl calls are made)
#   * every PPD's identity lines, cupsFilter chain and full option table,
#     including invocation code (cupsInteger/cupsString mappings show how the
#     vendor filter receives its settings)
#   * for each ELF: architecture, NEEDED libraries, imported symbols, and
#     strings that look like TSPL commands or printf formats
#   * a "hints" section flagging interesting imports (zlib => compressed
#     bitmaps, cupsBackChannel => status queries, ...)
#
# This is observation for interoperability only.  Never commit vendor
# binaries, PPDs or extracted text to this repository - commit *findings*
# (in your own words) to docs/REVERSE_ENGINEERING.md instead.

set -euo pipefail

die() { echo "inspect-vendor-pkg: $*" >&2; exit 1; }

[[ $# -ge 1 ]] || die "usage: $0 <package.zip|.deb|.tar.gz|dir> [outdir]"
SRC=$(readlink -f "$1")
OUT=$(readlink -m "${2:-./vendor-inspect}")
EX="$OUT/extracted"
REPORT="$OUT/report.md"
mkdir -p "$EX"

TSPL_RE='^(SIZE|GAP|BLINE|DIRECTION|REFERENCE|OFFSET|SPEED|DENSITY|SET |CLS|BITMAP|PRINT|CODEPAGE|SHIFT|HOME|FORMFEED|GAPDETECT|BLINEDETECT|AUTODETECT|SELFTEST|LIMITFEED|DOWNLOAD|EOP|~!|ZLIB|LZ77|PUTBMP)'

extract_one() {           # $1 = archive, $2 = destination dir
  local a=$1 d=$2
  mkdir -p "$d"
  case "$a" in
    *.zip|*.ZIP)
      if command -v unzip >/dev/null; then unzip -q -o "$a" -d "$d"
      else python3 -m zipfile -e "$a" "$d"; fi ;;
    *.deb)
      if command -v dpkg-deb >/dev/null; then
        dpkg-deb -x "$a" "$d/data"
        dpkg-deb -e "$a" "$d/DEBIAN"
      else
        (cd "$d" && ar x "$a" && mkdir -p data DEBIAN &&
         tar -xf data.tar.* -C data && tar -xf control.tar.* -C DEBIAN)
      fi ;;
    *.tar|*.tar.gz|*.tgz|*.tar.xz|*.tar.bz2|*.tar.zst) tar -xf "$a" -C "$d" ;;
    *.gz) gunzip -c "$a" > "$d/$(basename "${a%.gz}")" ;;
    *) return 1 ;;
  esac
}

if [[ -d "$SRC" ]]; then
  cp -a "$SRC/." "$EX/"
else
  extract_one "$SRC" "$EX" || die "unsupported package type: $SRC"
fi

# Unpack nested archives (up to 4 levels: zip -> deb -> tar -> gz)
for _ in 1 2 3 4; do
  found=0
  while IFS= read -r -d '' f; do
    marker="$f.unpacked"
    [[ -e "$marker" ]] && continue
    if extract_one "$f" "${f}.d" 2>/dev/null; then
      touch "$marker"; found=1
    fi
  done < <(find "$EX" -type f \( -name '*.zip' -o -name '*.deb' -o -name '*.tar*' \
                                  -o -name '*.tgz' -o -name '*.ppd.gz' \) -print0)
  [[ $found -eq 1 ]] || break
done

{
  echo "# Vendor package inventory"
  echo
  echo "- Source: \`$(basename "$SRC")\`"
  [[ -f "$SRC" ]] && echo "- SHA-256: \`$(sha256sum "$SRC" | cut -d' ' -f1)\`"
  echo "- Generated: $(date -u +%Y-%m-%dT%H:%MZ) by tools/re/inspect-vendor-pkg.sh"
  echo
  echo "## Files"
  echo
  echo "| Size | SHA-256 (16) | Path | Type |"
  echo "|---:|---|---|---|"
  find "$EX" -type f ! -name '*.unpacked' -print0 | sort -z |
  while IFS= read -r -d '' f; do
    rel=${f#"$EX"/}
    # shellcheck disable=SC2016  # backticks are literal Markdown
    printf '| %s | `%s` | `%s` | %s |\n' "$(stat -c %s "$f")" \
      "$(sha256sum "$f" | cut -c1-16)" "$rel" "$(file -b "$f" | cut -c1-70 | tr '|' '/')"
  done

  while IFS= read -r -d '' ctl; do
    dir=$(dirname "$ctl")
    echo
    echo "## Debian control: \`${dir#"$EX"/}\`"
    echo
    echo '```'
    cat "$ctl"
    echo '```'
    for s in preinst postinst prerm postrm; do
      [[ -f "$dir/$s" ]] || continue
      echo
      echo "### $s"
      echo
      echo '```sh'
      cat "$dir/$s"
      echo '```'
    done
  done < <(find "$EX" -path '*DEBIAN/control' -print0)

  while IFS= read -r -d '' ppd; do
    echo
    echo "## PPD: \`${ppd#"$EX"/}\`"
    echo
    echo '```'
    grep -E '^\*(ModelName|NickName|ShortNickName|Manufacturer|Product|1284DeviceID|PCFileName|cupsFilter2?|cupsModelNumber|cupsManualCopies|cupsVersion|FileVersion|HWMargins|MaxMedia(Width|Height)|ParamCustomPageSize)' "$ppd" || true
    echo '```'
    echo
    echo "### Options (keyword: default -> choices)"
    echo
    awk '
      /^\*OpenUI/        { split($0, a, /[*\/:]/); opt=a[3]; sub(/ .*/, "", opt); printf "- **%s**", opt; inopt=1; n=0; next }
      inopt && /^\*OrderDependency/ { next }
      /^\*Default/ && inopt { d=$0; sub(/^\*Default[^:]*: */, "", d); printf " (default `%s`):", d; next }
      /^\*CloseUI/       { print ""; inopt=0; next }
      inopt && /^\*[A-Za-z]/ {
        line=$0; sub(/^\*[^ ]+ /, "", line); kw=line; sub(/[\/:].*/, "", kw)
        inv=line; if (match(inv, /"[^"]*"/)) inv=substr(inv, RSTART, RLENGTH); else inv=""
        printf "%s `%s`%s", (n++ ? "," : ""), kw, (inv != "\"\"" && inv != "" ? " " inv : "")
      }' "$ppd"
  done < <(find "$EX" -type f \( -iname '*.ppd' \) -print0)

  while IFS= read -r -d '' elf; do
    rel=${elf#"$EX"/}
    echo
    echo "## ELF: \`$rel\`"
    echo
    echo "- Type: $(file -b "$elf")"
    echo "- Machine: $(readelf -h "$elf" 2>/dev/null | awk -F: '/Machine/ {gsub(/^ +/, "", $2); print $2}')"
    echo "- NEEDED: $(readelf -d "$elf" 2>/dev/null | awk -F'[][]' '/NEEDED/ {printf "%s ", $2}')"
    echo
    echo "### Imported symbols"
    echo
    echo '```'
    nm -D --undefined-only "$elf" 2>/dev/null | awk '{print $NF}' | sort -u | tr '\n' ' ' | fold -s -w 100
    echo
    echo '```'
    echo
    echo "### TSPL-looking strings"
    echo
    echo '```'
    strings -n 3 "$elf" | grep -E "$TSPL_RE" | sort -u | head -200 || true
    echo '```'
    echo
    echo "### printf formats and option names"
    echo
    echo '```'
    strings -n 4 "$elf" | grep -E '%[0-9.]*[dfsux]|cupsInteger|Darkness|Density|Speed|Gap|Halftone|Dither|Media' \
      | grep -vE '^(GLIBC|_)' | sort -u | head -200 || true
    echo '```'

    syms=$(nm -D --undefined-only "$elf" 2>/dev/null | awk '{print $NF}')
    echo
    echo "### Hints"
    echo
    hints=()
    grep -qE '^(compress|compress2|deflate)' <<<"$syms" && hints+=("links zlib compressors: look for a compressed BITMAP / ZLIB command variant")
    grep -q 'cupsBackChannel' <<<"$syms" && hints+=("reads the back-channel: the filter queries printer status mid-job")
    grep -q 'cupsSideChannel' <<<"$syms" && hints+=("uses the side-channel (device ID / drain / soft-reset requests)")
    grep -qE '^ppd(Open|FindMarkedChoice|FindChoice)' <<<"$syms" && hints+=("reads options directly from the PPD (not only via raster header)")
    grep -qE 'cupsRasterReadHeader2?' <<<"$syms" && hints+=("standard CUPS raster consumer")
    grep -qE '^setlocale' <<<"$syms" && hints+=("calls setlocale(): check decimal formatting under non-C locales")
    [[ ${#hints[@]} -gt 0 ]] || hints=("none of the tracked imports were found")
    printf -- '- %s\n' "${hints[@]}"
  done < <(find "$EX" -type f -exec sh -c 'head -c4 "$1" | grep -q "ELF" && printf "%s\0" "$1"' _ {} \;)
} > "$REPORT"

echo "inspect-vendor-pkg: report written to $REPORT"
echo "inspect-vendor-pkg: extracted tree in $EX (do not commit it)"
