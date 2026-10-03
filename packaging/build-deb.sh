#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The sp410-cups-driver contributors.
#
# build-deb.sh - build a binary .deb for the host architecture
# (amd64, arm64 or armhf on Raspberry Pi OS) without debhelper.
#
#   packaging/build-deb.sh [version]      -> build/sp410-cups-driver_<v>_<arch>.deb
#
# Respects CUPS_CFLAGS/CUPS_LIBS like the Makefile.

set -euo pipefail
cd "$(dirname "$0")/.."

VERSION=${1:-$(cat VERSION)}
ARCH=$(dpkg --print-architecture 2>/dev/null || uname -m)
PKG=sp410-cups-driver
STAGE=build/deb/${PKG}_${VERSION}_${ARCH}
OUTDEB=build/${PKG}_${VERSION}_${ARCH}.deb

rm -rf "$STAGE"
make -s all
make -s install DESTDIR="$PWD/$STAGE" PREFIX=/usr FILTERDIR=/usr/lib/cups/filter \
     UDEVDIR=/usr/lib/udev/rules.d

# Debian policy: compressed changelog + copyright in the doc dir.
DOC="$STAGE/usr/share/doc/$PKG"
gzip -9n -c CHANGELOG.md > "$DOC/changelog.gz"
cp LICENSE "$DOC/copyright"

mkdir -p "$STAGE/DEBIAN"
INSTALLED_KB=$(du -sk "$STAGE" | cut -f1)

# libcups was renamed libcups2t64 in Debian trixie / Ubuntu 24.04.
cat > "$STAGE/DEBIAN/control" <<EOF
Package: $PKG
Version: $VERSION
Architecture: $ARCH
Maintainer: sp410-cups-driver contributors <noreply@example.invalid>
Installed-Size: $INSTALLED_KB
Depends: libc6, libcups2 (>= 2.2) | libcups2t64, cups (>= 2.2) | cups-daemon (>= 2.2), python3
Recommends: cups-filters
Section: misc
Priority: optional
Homepage: https://github.com/rightiswrong/sp410-cups-driver
Description: Open-source CUPS driver for iDPRT SP410-family label printers
 Clean-room CUPS raster filter that converts print jobs to TSPL for the
 iDPRT SP410, SP410BT and SP420 203-dpi direct-thermal label printers.
 Unlike the vendor package it builds for any architecture, including the
 Raspberry Pi (armhf/arm64).  Includes PPDs, a udev rule, the sp410ctl
 utility and the tspl-decode stream inspector.
EOF

cat > "$STAGE/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = configure ] && command -v udevadm >/dev/null 2>&1; then
  udevadm control --reload-rules 2>/dev/null || true
  udevadm trigger --subsystem-match=usb --attr-match=idVendor=20d1 2>/dev/null || true
fi
exit 0
EOF
cat > "$STAGE/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
if command -v udevadm >/dev/null 2>&1; then
  udevadm control --reload-rules 2>/dev/null || true
fi
exit 0
EOF
chmod 0755 "$STAGE/DEBIAN/postinst" "$STAGE/DEBIAN/postrm"

dpkg-deb --root-owner-group --build "$STAGE" "$OUTDEB" >/dev/null
echo "built $OUTDEB"
