#!/bin/bash
# The macOS package, checked on a runner (sudo without a password):
#   packaging/macos/check-install.sh dist/dussek-<version>-macos.pkg <portable folder>
# Installs, reinstalls over a version that left an instrument behind, lists the instruments
# through the installed apps (Dussek's and Weidinger's), checks the ad hoc signatures, that every
# binary carries both chips (arm64, x86_64), and the AudioUnits with Apple's auval (Logic loads none that fails).
set -eu
pkg=${1:?usage: check-install.sh <pkg> <portable folder>}
dist=${2:?usage: check-install.sh <pkg> <portable folder>}
lib="/Library/Application Support/Dussek/instruments"
plugins=/Library/Audio/Plug-Ins
fail() { echo "check-install: $*" >&2; exit 1; }

# the unpacked archive itself, nothing installed: its app plays the instruments beside it
"$dist/Dussek.app/Contents/MacOS/dussek" list | grep -q "^harp	" || fail "the archive's app does not find its instruments"
"$dist/Weidinger.app/Contents/MacOS/weidinger" list | grep -q "^trumpet	" || fail "the archive's Weidinger does not find its instruments"

sudo installer -pkg "$pkg" -target / || fail "the first install failed"
sudo mkdir -p "$lib/stale"
sudo cp "$lib/harp/instrument.json" "$lib/harp/data.bin" "$lib/stale/"
sudo installer -pkg "$pkg" -target / || fail "the update failed"

for f in "/Library/Application Support/Dussek/THIRD-PARTY-NOTICES.txt" "/Library/Application Support/Dussek/LICENSE.txt" \
         /Applications/Dussek.app "$plugins/LV2/dussek.lv2" "$plugins/CLAP/dussek.clap" "$plugins/VST3/dussek.vst3" \
         "$plugins/Components/dussek.component" \
         /Applications/Weidinger.app "$plugins/LV2/weidinger.lv2" "$plugins/CLAP/weidinger.clap" "$plugins/VST3/weidinger.vst3" \
         "$plugins/Components/weidinger.component"; do
    [ -e "$f" ] || fail "missing: $f"
done
# the bundles' signatures; the LV2's (a plain folder) on its libraries
for f in /Applications/Dussek.app "$plugins/CLAP/dussek.clap" "$plugins/VST3/dussek.vst3" "$plugins/Components/dussek.component" \
         /Applications/Weidinger.app "$plugins/CLAP/weidinger.clap" "$plugins/VST3/weidinger.vst3" "$plugins/Components/weidinger.component" \
         $(find "$plugins/LV2/dussek.lv2" "$plugins/LV2/weidinger.lv2" -type f \( -name '*.dylib' -o -name '*.so' \)); do
    codesign -v "$f" || fail "no valid signature: $f"
done
# every binary for both chips (lipo -archs: "x86_64 arm64")
for p in dussek weidinger; do
    app=$([ $p = weidinger ] && echo Weidinger || echo Dussek)
    for f in "/Applications/$app.app/Contents/MacOS/$p" "$plugins/CLAP/$p.clap/Contents/MacOS/$p" \
             "$plugins/VST3/$p.vst3/Contents/MacOS/$p" "$plugins/Components/$p.component/Contents/MacOS/$p" \
             $(find "$plugins/LV2/$p.lv2" -type f \( -name '*.dylib' -o -name '*.so' \)); do
        archs=$(lipo -archs "$f") || fail "not a binary: $f"
        case " $archs " in *" arm64 "*) ;; *) fail "no arm64 in $f ($archs)" ;; esac
        case " $archs " in *" x86_64 "*) ;; *) fail "no x86_64 in $f ($archs)" ;; esac
    done
done
# the AudioUnits as Logic checks them: the registrar restarted so it sees the new components
sudo killall -9 AudioComponentRegistrar 2>/dev/null || true
for id in "aumu Dsek Ccrs" "aumu Wdgr Ccrs"; do
    out=$(auval -v $id 2>&1) || { printf '%s\n' "$out" | tail -n 40; fail "auval failed: $id"; }
    printf '%s\n' "$out" | grep -q "AU VALIDATION SUCCEEDED" || { printf '%s\n' "$out" | tail -n 40; fail "auval did not pass: $id"; }
done
list=$(/Applications/Dussek.app/Contents/MacOS/dussek list) || fail "dussek list found no instruments"
brass=$(/Applications/Weidinger.app/Contents/MacOS/weidinger list) || fail "weidinger list found no instruments"
printf '%s\n' "$brass" | grep -q "^trumpet	.*/Library/Application Support/Dussek/" || fail "the brass do not come from /Library"
missing=$(printf '%s\n%s\n' "$list" "$brass" | "$(dirname "$0")/../unlisted.sh" "$dist/instruments")
[ -z "$missing" ] || fail "shipped but not listed: $(echo $missing)"
listed=$(printf '%s\n' "$list" | grep -vc '^#' || true)
printf '%s\n' "$list" | grep -q "^stale	" && fail "a removed instrument stayed"
printf '%s\n' "$list" | grep -q "^harp	.*/Library/Application Support/Dussek/" || fail "the instruments do not come from /Library"
listed=$((listed + $(printf '%s\n' "$brass" | grep -vc '^#' || true)))
echo "check-install: ok ($listed instruments listed)"
