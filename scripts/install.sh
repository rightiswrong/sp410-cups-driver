#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The sp410-cups-driver contributors.
#
# install.sh - build and install sp410-cups-driver from source, optionally
# creating a CUPS queue for a connected printer.
#
#   sudo ./scripts/install.sh                       # install driver only
#   sudo ./scripts/install.sh --add-queue           # + queue "SP410" on the first iDPRT USB URI
#   sudo ./scripts/install.sh --add-queue Labels --model sp420 --uri usb://...
#   sudo ./scripts/install.sh --deps                # also apt-get the build dependencies
#
# Works on Raspberry Pi OS (armhf/arm64), Debian, Ubuntu; on other distros
# install a C compiler, make, CUPS + its development headers first.

set -euo pipefail
cd "$(dirname "$0")/.."

QUEUE='' MODEL=sp410 URI='' DEPS=0 MEDIA=w288h432

while [[ $# -gt 0 ]]; do
  case "$1" in
    --add-queue) QUEUE=SP410; if [[ ${2:-} && ${2:0:1} != - ]]; then QUEUE=$2; shift; fi; shift ;;
    --model)     MODEL=$2; shift 2 ;;
    --uri)       URI=$2; shift 2 ;;
    --media)     MEDIA=$2; shift 2 ;;
    --deps)      DEPS=1; shift ;;
    -h|--help)   sed -n '4,15p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

[[ $EUID -eq 0 ]] || { echo "please run with sudo" >&2; exit 1; }

if [[ $DEPS -eq 1 ]]; then
  command -v apt-get >/dev/null || { echo "--deps only supports apt-based systems" >&2; exit 1; }
  apt-get update
  apt-get install -y build-essential libcups2-dev cups cups-filters python3
fi

missing=()
command -v cc >/dev/null   || missing+=(build-essential)
command -v make >/dev/null || missing+=(make)
command -v cups-config >/dev/null || pkg-config --exists cups 2>/dev/null || missing+=(libcups2-dev)
command -v lpadmin >/dev/null || missing+=(cups)
if [[ ${#missing[@]} -gt 0 ]]; then
  echo "missing build dependencies: ${missing[*]}" >&2
  echo "install them (e.g. sudo apt-get install ${missing[*]}) or re-run with --deps" >&2
  exit 1
fi

# Build as the invoking user when possible so the tree isn't root-owned.
if [[ -n "${SUDO_USER:-}" ]]; then
  sudo -u "$SUDO_USER" make -s all
else
  make -s all
fi
make -s install PREFIX=/usr

if command -v udevadm >/dev/null; then
  udevadm control --reload-rules || true
  udevadm trigger --subsystem-match=usb --attr-match=idVendor=20d1 || true
fi

PPD=/usr/share/ppd/sp410-cups-driver/idprt-$MODEL.ppd
[[ -f "$PPD" ]] || { echo "unknown model '$MODEL' (sp410, sp410bt, sp420)" >&2; exit 1; }
echo "installed: $(cups-config --serverbin 2>/dev/null || echo /usr/lib/cups)/filter/sp410-rastertotspl, $PPD"

if [[ -n "$QUEUE" ]]; then
  if [[ -z "$URI" ]]; then
    URI=$(lpinfo --include-schemes usb -v 2>/dev/null | awk '{print $2}' \
          | grep -iE 'idprt|sp4[12]0|hprt|20d1' | head -n1 || true)
  fi
  if [[ -z "$URI" ]]; then
    echo "no iDPRT USB printer found. Connected USB printers:" >&2
    lpinfo --include-schemes usb -v >&2 || true
    echo "re-run with --uri <one of the above>" >&2
    exit 1
  fi
  lpadmin -p "$QUEUE" -E -v "$URI" -P "$PPD" -o PageSize="$MEDIA" \
          -D "iDPRT ${MODEL^^} (sp410-cups-driver)"
  echo "queue '$QUEUE' -> $URI"
  echo "test it:   lp -d $QUEUE -o PageSize=$MEDIA /usr/share/cups/data/testprint"
fi

if [[ -n "${SUDO_USER:-}" ]] && ! id -nG "$SUDO_USER" | grep -qw lp; then
  echo "tip: 'sudo usermod -aG lp $SUDO_USER' lets you use sp410ctl without sudo"
fi
