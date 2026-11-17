#!/bin/sh
# Each plugin's icon, 512 px: dussek.svg (a keyboard on three plates; from a generator script in the maintainer's
# notes), weidinger.svg (a trumpet on the same plates) → the PNGs (Linux's desktop entries, the README), the .ico
# (Windows), the .icns (macOS), and the standalones' window icon on X11 (plugin/src/ui/WindowIcon-<name>.inc,
# x11_icon.py).
# Needs rsvg-convert, ImageMagick, png2icns (libicns) and Python's PIL; the results are committed.
set -eu
cd "$(dirname "$0")"
for p in dussek weidinger; do
    for s in 16 32 48 64 128 256 512; do
        rsvg-convert -w "$s" -h "$s" "$p.svg" -o "$p-$s.png"
    done
    cp "$p-256.png" "$p.png"
    magick "$p-16.png" "$p-32.png" "$p-48.png" "$p-256.png" "$p.ico"
    png2icns "$p.icns" "$p-16.png" "$p-32.png" "$p-128.png" "$p-256.png" "$p-512.png" >/dev/null 2>&1
    python3 x11_icon.py "../../plugin/src/ui/WindowIcon-$p.inc" "$p.svg" "$p-32.png" "$p-64.png" "$p-128.png"
    rm -f "$p-16.png" "$p-32.png" "$p-48.png" "$p-64.png" "$p-128.png" "$p-256.png" "$p-512.png"
done
