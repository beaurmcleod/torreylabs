#!/bin/bash
# Installs Tether into your user plug-in folders and clears the download quarantine.
set -e
cd "$(dirname "$0")"

VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3_DIR" "$AU_DIR"

rm -rf "$VST3_DIR/Tether.vst3" "$AU_DIR/Tether.component"
cp -R Tether.vst3 "$VST3_DIR/"
cp -R Tether.component "$AU_DIR/"
xattr -dr com.apple.quarantine "$VST3_DIR/Tether.vst3" "$AU_DIR/Tether.component" 2>/dev/null || true

# Make Logic / GarageBand notice the new Audio Unit.
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo "Tether installed. Restart your DAW and rescan plug-ins."
