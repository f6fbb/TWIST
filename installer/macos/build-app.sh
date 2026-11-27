#!/bin/bash
# build-app.sh - TWIST Console.app for Intel Macs, macOS 10.15 or later,
# from the repository root:
#
#   installer/macos/build-deps.sh deps     # once (FFTW, PortAudio, Hamlib)
#   installer/macos/build-app.sh deps      # -> dist/TWIST-Console-<version>-mac-intel.zip and .dmg
#
# TWIST Console.app/Contents/
#   Info.plist        name, version, minimum macOS, why the microphone
#   MacOS/Twist       the application (.NET, self-contained) and its libraries
#                     (libtwcore, libtwist, libopera, libpi4 .dylib)
#   MacOS/hamlib/     rigctld, rigctl
#   MacOS/            also the user's guide (PDF, Word) and the testers' guide
#   Resources/        icon
# Signed "ad hoc" only (no Apple developer certificate): the first time,
# right click › Ouvrir in the Finder.
set -euo pipefail
DEPS=$(cd "$1" && pwd)
VER=$(sed -n 's:.*<Version>\(.*\)</Version>.*:\1:p' app/Twist.App/Twist.App.csproj | head -1)

# the engine: FFTW and PortAudio linked in (static), CoreAudio frameworks
make clean >/dev/null
installer/macos/make.sh "$DEPS"
otool -L libtwcore.dylib libtwist.dylib libopera.dylib libpi4.dylib

APP="dist/TWIST Console.app"
rm -rf dist && mkdir -p "$APP/Contents/MacOS/hamlib" "$APP/Contents/Resources"
dotnet publish app/Twist.App -c Release -r osx-x64 --self-contained true -nologo -o "$APP/Contents/MacOS"
cp libtwcore.dylib libtwist.dylib libopera.dylib libpi4.dylib "$APP/Contents/MacOS/"
cp "$DEPS/bin/rigctld" "$DEPS/bin/rigctl" "$APP/Contents/MacOS/hamlib/"
cp "$DEPS"/COPYING* "$DEPS"/README "$APP/Contents/MacOS/hamlib/" 2>/dev/null || true
cp docs/TESTEURS.md "$APP/Contents/MacOS/TESTEURS.txt"
cp LICENSE CREDITS.md "$APP/Contents/MacOS/"
cp docs/TWIST-Console-Guide-utilisateur.pdf docs/TWIST-Console-Guide-utilisateur.docx "$APP/Contents/MacOS/"
sed "s/@VERSION@/$VER/g" installer/macos/Info.plist > "$APP/Contents/Info.plist"
python3 installer/macos/icns.py app/Twist.App/Assets/twist.ico "$APP/Contents/Resources/twist.icns"

# every Mach-O file must load on 10.15
bad=0
while IFS= read -r f; do
  v=$(otool -arch x86_64 -l "$f" 2>/dev/null | awk '/LC_BUILD_VERSION/{b=1} b&&/minos/{print $2; exit} /LC_VERSION_MIN_MACOSX/{m=1} m&&/version/{print $2; exit}')
  case "$v" in
    10.[0-9]|10.[0-9].*|10.1[0-5]|10.1[0-5].*) ;;
    "") echo "no minimum version: $f" ;;
    *) echo "too new for 10.15: $f ($v)"; bad=1 ;;
  esac
done < <(find "$APP" -type f \( -name '*.dylib' -o -perm -u+x \) -exec sh -c 'file -b "$1" | grep -q Mach-O && echo "$1"' _ {} \;)
[ $bad -eq 0 ] || exit 1

codesign --force --deep --sign - "$APP"
codesign --verify --deep "$APP"
(cd dist && ditto -c -k --keepParent "TWIST Console.app" "TWIST-Console-$VER-mac-intel.zip")
hdiutil create -volname "TWIST Console $VER" -srcfolder "$APP" -ov -format UDZO "dist/TWIST-Console-$VER-mac-intel.dmg" >/dev/null
ls -l dist
