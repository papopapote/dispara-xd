#!/bin/sh
# Compila la versión de escritorio para Mac (usa SDL2.framework en ~/ps4-dev/mac).
set -e
cd "$(dirname "$0")"
FW="$HOME/ps4-dev/mac"
mkdir -p build/mac/DisparaXD.app/Contents/MacOS build/mac/DisparaXD.app/Contents/Frameworks
clang -O2 -Wall -Wextra -Wno-unused-parameter -F"$FW" -o build/mac/DisparaXD.app/Contents/MacOS/DisparaXD \
  src/game.c src/platform_sdl.c -framework SDL2 -Wl,-rpath,@executable_path/../Frameworks
rm -rf build/mac/DisparaXD.app/Contents/Frameworks/SDL2.framework
cp -R "$FW/SDL2.framework" build/mac/DisparaXD.app/Contents/Frameworks/
cat > build/mac/DisparaXD.app/Contents/Info.plist <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>DisparaXD</string>
<key>CFBundleIdentifier</key><string>local.disparaxd.juego</string>
<key>CFBundleName</key><string>Dispara XD</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>NSHighResolutionCapable</key><false/>
</dict></plist>
PL
echo "Listo: build/mac/DisparaXD.app"
