#!/bin/sh
# Compila Dispara XD para PS4 (homebrew, GoldHEN) y genera el .pkg
set -e
cd "$(dirname "$0")"
export OO_PS4_TOOLCHAIN="${OO_PS4_TOOLCHAIN:-$HOME/ps4-dev/OpenOrbis/PS4Toolchain}"
TC="$OO_PS4_TOOLCHAIN"; BIN="$TC/bin/macos"
LLD="$HOME/ps4-dev/venv/bin/python -m ziglang ld.lld"
TITLE="Dispara XD"; VERSION="1.00"; TITLE_ID="DSXD00001"; CONTENT_ID="IV0000-DSXD00001_00-DISPARAXD0000000"
OUT=build/ps4; mkdir -p $OUT
CFLAGS="--target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -O2 -DPLATFORM_PS4 -isysroot $TC -isystem $TC/include -Wall -Wno-unused-parameter"

echo "== compilar"
clang $CFLAGS -c -o $OUT/game.o src/game.c
clang $CFLAGS -c -o $OUT/platform_sdl.o src/platform_sdl.c

echo "== enlazar"
$LLD -m elf_x86_64 -pie --script "$TC/link.x" --eh-frame-hdr -o $OUT/dispara.elf "$TC/lib/crt1.o" $OUT/game.o $OUT/platform_sdl.o \
  -L"$TC/lib" -lSDL2 -lc -lkernel -lc++ -lSceUserService -lSceVideoOut -lSceAudioOut -lScePad -lSceSysmodule

echo "== eboot"
cd ps4
"$BIN/create-fself-macos" -in=../$OUT/dispara.elf -out=../$OUT/dispara.oelf --eboot "eboot.bin" --paid 0x3800000000000011 >/dev/null

echo "== param.sfo"
SFO=sce_sys/param.sfo; rm -f $SFO
"$BIN/PkgTool.Core" sfo_new $SFO
set_e() { "$BIN/PkgTool.Core" sfo_setentry $SFO "$@"; }
set_e APP_TYPE --type Integer --maxsize 4 --value 1
set_e APP_VER --type Utf8 --maxsize 8 --value "$VERSION"
set_e ATTRIBUTE --type Integer --maxsize 4 --value 0
set_e CATEGORY --type Utf8 --maxsize 4 --value gd
set_e CONTENT_ID --type Utf8 --maxsize 48 --value "$CONTENT_ID"
set_e DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
set_e SYSTEM_VER --type Integer --maxsize 4 --value 0
set_e TITLE --type Utf8 --maxsize 128 --value "$TITLE"
set_e TITLE_ID --type Utf8 --maxsize 12 --value "$TITLE_ID"
set_e VERSION --type Utf8 --maxsize 8 --value "$VERSION"

echo "== pkg"
rm -f pkg.gp4
"$BIN/create-gp4" -out pkg.gp4 --content-id="$CONTENT_ID" --files "eboot.bin sce_sys/about/right.sprx sce_sys/param.sfo sce_sys/icon0.png"
"$BIN/PkgTool.Core" pkg_build pkg.gp4 ../build
cd ..
ls -la build/*.pkg
