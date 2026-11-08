#!/bin/sh
# Compila Dispara XD para PS3 (PSL1GHT, CFW/HEN) y genera el .pkg
# Toolchain en ~/ps3-dev (ps3dev precompilado + PSL1GHT compilado localmente).
set -e
cd "$(dirname "$0")"
export PS3DEV="$HOME/ps3-dev/ps3dev"
export PSL1GHT="$PS3DEV"
export PATH="$PS3DEV/bin:$PS3DEV/ppu/bin:$PATH"
export DYLD_FALLBACK_LIBRARY_PATH="$HOME/ps3-dev/zstd/lib"

TITLE="Dispara XD"
APPID="DSXD00001"
CONTENTID="UP0001-${APPID}_00-0000000000000000"
OUT=build/ps3
mkdir -p $OUT

CFLAGS="-O2 -Wall -Wno-unused-parameter -mcpu=cell -mhard-float -fmodulo-sched -ffunction-sections -fdata-sections -I$PS3DEV/ppu/include -I$PS3DEV/ppu/include/simdmath"
LDFLAGS="-mhard-float -fmodulo-sched -ffunction-sections -fdata-sections -Wl,--gc-sections"
LIBS="-L$PS3DEV/ppu/lib -lgcm_sys -lrsx -lsysutil -lio -laudio -lm -lrt -llv2"

quiet() { grep -v "newer than running OS" || true; }

echo "== compilar"
ppu-gcc $CFLAGS -c src/game.c -o $OUT/game.o 2>&1 | quiet
ppu-gcc $CFLAGS -c src/platform_ps3.c -o $OUT/platform_ps3.o 2>&1 | quiet

echo "== enlazar"
ppu-gcc $LDFLAGS $OUT/game.o $OUT/platform_ps3.o $LIBS -o $OUT/dispara.elf 2>&1 | quiet
test -f $OUT/dispara.elf

if [ "$1" = "elf" ]; then echo "solo ELF"; exit 0; fi

echo "== self / eboot"
ppu-strip $OUT/dispara.elf -o $OUT/dispara.stripped.elf
sprxlinker $OUT/dispara.stripped.elf
rm -rf $OUT/pkg && mkdir -p $OUT/pkg/USRDIR
make_self_npdrm $OUT/dispara.stripped.elf $OUT/pkg/USRDIR/EBOOT.BIN "$CONTENTID" > /dev/null
cp ps3/ICON0.PNG $OUT/pkg/ICON0.PNG

echo "== param.sfo"
sfo --title "$TITLE" --appid "$APPID" -f "$PS3DEV/bin/sfo.xml" $OUT/pkg/PARAM.SFO

echo "== pkg"
pkg --contentid "$CONTENTID" $OUT/pkg/ build/DisparaXD_PS3.pkg > /dev/null
package_finalize build/DisparaXD_PS3.pkg
ls -la build/DisparaXD_PS3.pkg
