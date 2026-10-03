#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The sp410-cups-driver contributors.
#
# uninstall.sh - remove a source install of sp410-cups-driver.
#
#   sudo ./scripts/uninstall.sh                  # remove files, keep queues
#   sudo ./scripts/uninstall.sh --remove-queues  # also delete queues using our PPD
#
# (If you installed the .deb, use `sudo apt remove sp410-cups-driver`.)

set -euo pipefail
cd "$(dirname "$0")/.."
[[ $EUID -eq 0 ]] || { echo "please run with sudo" >&2; exit 1; }

if [[ ${1:-} == --remove-queues ]]; then
  for ppd in /etc/cups/ppd/*.ppd; do
    [[ -e "$ppd" ]] || continue
    if grep -q 'sp410-cups-driver' "$ppd"; then
      q=$(basename "$ppd" .ppd)
      lpadmin -x "$q" && echo "removed queue $q"
    fi
  done
fi

make -s uninstall PREFIX=/usr
if command -v udevadm >/dev/null; then udevadm control --reload-rules || true; fi
echo "sp410-cups-driver removed"
