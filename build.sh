#!/bin/sh
# openamigaxml: libxml2 and Expat, built for AmigaOS 3.x (68020 + FPU) with the
# os32-gcc16 compiler (bebbo's amiga-gcc, GCC 16.2, libnix, libpthread).
# MIT, Copyright (c) 2026 Dalsin Limited. The library keeps its own licence.
#
#   OS32_GCC16   compiler root holding prefix/ and compat/
#                (default ~/AmigaChrome/stoves/os32-gcc16)
#   PREFIX       where include/ and lib/ go (default ./out)
#   TARBALLS     folder holding the upstream tarballs listed in SOURCES
#                (default ./tarballs); the script checks their SHA-256
#   JOBS         parallel jobs for CMake/make builds (default 2)
#
# usage: ./build.sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
S=${OS32_GCC16:-"$HOME/AmigaChrome/stoves/os32-gcc16"}
P=$S/prefix
OUT=${PREFIX:-"$HERE/out"}
TARBALLS=${TARBALLS:-"$HERE/tarballs"}
JOBS=${JOBS:-2}
WORK="$HERE/work"
CC="$P/bin/m68k-amigaos-gcc"
CXX="$P/bin/m68k-amigaos-g++"
AR="$P/bin/m68k-amigaos-ar"
CPU=${OS32_CPU_FLAGS:-"-m68020 -m68881 -mcrt=nix20"}
CFLAGS="-O2 $CPU -D_DEFAULT_SOURCE=1 -D_POSIX_TIMERS=1 -D_POSIX_REALTIME_SIGNALS=1 -fno-common"
mkdir -p "$OUT/include" "$OUT/lib" "$WORK"

# unpack NAME TARBALL SHA256: check the tarball and unpack it into $WORK
unpack() {
    t="$TARBALLS/$2"
    [ -f "$t" ] || { echo "missing $t (see SOURCES)"; exit 2; }
    echo "$3  $t" | sha256sum -c - >/dev/null || { echo "SHA-256 mismatch: $t"; exit 2; }
    rm -rf "$WORK/$1"; mkdir -p "$WORK/$1"
    case "$2" in
        *.zip) (cd "$WORK/$1" && unzip -q "$t") ;;
        *) tar xf "$t" -C "$WORK/$1" ;;
    esac
}

# archive NAME FILE...: compile into $OUT/lib/libNAME.a ($XFLAGS added)
archive() {
    name=$1; shift
    obj="$WORK/obj-$name"
    rm -rf "$obj"; mkdir -p "$obj"
    for f in "$@"; do
        o="$obj/$(echo "$f" | tr '/' '_' | sed 's/\.[a-z]*$//').o"
        case "$f" in
            *.cc|*.cpp) $CXX $CFLAGS ${XFLAGS:-} -c "$f" -o "$o" ;;
            *) $CC $CFLAGS ${XFLAGS:-} -c "$f" -o "$o" ;;
        esac
    done
    rm -f "$OUT/lib/lib$name.a"
    $AR rcs "$OUT/lib/lib$name.a" "$obj"/*.o
    echo "lib$name.a: $(wc -c < "$OUT/lib/lib$name.a") bytes"
}

TOOLCHAIN=${TOOLCHAIN_FILE:?set TOOLCHAIN_FILE to openamigabrowser/toolchain/amigaos3-gcc16.cmake}
unpack libxml2 libxml2-2.15.4.tar.xz 98087fd181d9070724f3fbc65c7377db03038eb92bd882374daff44940138821
cd "$WORK/libxml2/libxml2-2.15.4"
# Hunk executables have no position-independent code (-fPIC makes GOT
# references the assembler rejects).
sed -i 's/POSITION_INDEPENDENT_CODE ON/POSITION_INDEPENDENT_CODE OFF/' CMakeLists.txt
mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE=-O2 \
    -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX="$OUT" -DLIBXML2_WITH_ICONV=OFF -DLIBXML2_WITH_ICU=OFF \
    -DLIBXML2_WITH_MODULES=OFF -DLIBXML2_WITH_PROGRAMS=OFF -DLIBXML2_WITH_TESTS=OFF -DLIBXML2_WITH_PYTHON=OFF \
    -DLIBXML2_WITH_CATALOG=OFF -DLIBXML2_WITH_DEBUG=OFF -DLIBXML2_WITH_ZLIB=OFF -DLIBXML2_WITH_LZMA=OFF \
    -DLIBXML2_WITH_THREADS=ON
make -j"$JOBS" install

unpack expat expat-2.8.2.tar.bz2 69e7f52417d85b1c2b7fe855e176eec55d0b2d7d92d691372d833a1c7df7923b
cd "$WORK/expat/expat-2.8.2"
XFLAGS="-I$HERE/config/expat -Ilib -DHAVE_EXPAT_CONFIG_H" archive expat lib/xmlparse.c lib/xmlrole.c lib/xmltok.c
cp lib/expat.h lib/expat_external.h "$OUT/include/"
