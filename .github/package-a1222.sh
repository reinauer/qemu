#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Package the installed QEMU in stage/ as qemu-a1222-<platform>, with the
# A1222 documentation. Usage: package-a1222.sh <platform>

set -e
platform=$1
name=qemu-a1222-$platform

rm -rf "$name"
cp -R stage "$name"
# Drop what the internal libfdt build installs for developers.
rm -rf "$name/include"
if [ -d "$name/lib" ]; then
    find "$name/lib" \( -name '*.a' -o -name pkgconfig \) -prune \
        -exec rm -rf {} +
    find "$name/lib" -type d -empty -delete
fi
mkdir -p "$name/doc"
cp docs/system/ppc/a1222.rst scripts/a1222-sd-env.py "$name/doc/"
cp COPYING "$name/doc/"
git log -1 --format='Built from %H (%cd)' > "$name/doc/VERSION.txt"

cat > "$name/README.txt" <<'EOF'
QEMU with the AmigaOne A1222 machine (qemu-system-ppc -M a1222)

doc/a1222.rst describes the machine and how to boot AmigaOS 4.1.
AmigaOS, its Kickstart and the bboot loader are not included.

macOS: the binary is not notarized. After unpacking, run
  xattr -dr com.apple.quarantine qemu-a1222-macos-arm64
Linux: the binary needs the distribution's GLib, pixman, libslirp, GTK 3,
SDL 2, PulseAudio, ALSA and PipeWire libraries.
EOF

case $platform in
windows-*)
    zip -qr "$name.zip" "$name"
    ;;
*)
    tar -cJf "$name.tar.xz" "$name"
    ;;
esac
ls -l "$name".*
