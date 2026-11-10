#!/bin/sh
# Builds the Windows 98 cross compiler: GCC 13.2 for i686-w64-mingw32 with the OLD msvcrt.dll
# runtime and the win32 thread model (what Ubuntu's mingw-w64 package used to give).
# xPack / most new MinGW builds use UCRT (api-ms-win-crt-*.dll) and POSIX threads: those
# EXEs do not start on Windows 98, so do not use them for SC55W.EXE.
#
# Result: $G/prefix/bin/i686-w64-mingw32-{gcc,g++}. Then:
#   export PATH=$G/tools/bin:$G/prefix/bin:$PATH
#   sh tools/build-deps.sh && sh build98.sh && python3 tools/chkimp2.py out/SC55W.EXE
# The import check must show only GetModuleHandleW and LoadLibraryW as W-STUB (as shipped).
#
# Sources (git clone from github.com, the only site reachable from the build box):
#   $G/src/gcc        gcc-mirror/gcc tag releases/gcc-13.2.0
#   $G/src/mingw-w64  mingw-w64/mingw-w64 tag v11.0.1
#   $G/src/gmp        alisw/GMP v6.2.1          (needs MAKEINFO=true)
#   $G/src/mpfr       BrianGladman/mpfr (4.3-dev, run autoreconf -i first)
#   $G/src/mpc        BrianGladman/mpc  (copy the generated mpc/src/mpc.h from the build dir
#                     into src/mpc/src/ if the gcc build cannot find it)
#   flex 2.6.4        westes/flex v2.6.4 -> $G/tools (gengtype-lex needs it)
# Binutils: any i686-w64-mingw32 as/ld 2.4x (we symlink xPack's into $G/prefix/bin).
set -e
G=${G:-/home/claude/gccb}; P=$G/prefix; T=i686-w64-mingw32
export PATH=$G/tools/bin:$P/bin:$PATH
for d in gmp mpfr mpc; do [ -e $G/src/gcc/$d ] || ln -s ../$d $G/src/gcc/$d; done

# 1. headers (default CRT msvcrt, _WIN32_WINNT 0x0601 as compat98.c expects)
mkdir -p $G/hdr && cd $G/hdr
$G/src/mingw-w64/mingw-w64-headers/configure --host=$T --prefix=$P/$T \
  --with-default-msvcrt=msvcrt --with-default-win32-winnt=0x0601
make install
[ -e $P/mingw ] || ln -s $T $P/mingw

# 2. the compiler itself
mkdir -p $G/gcc1 && cd $G/gcc1
$G/src/gcc/configure --target=$T --prefix=$P --with-sysroot=$P --with-native-system-header-dir=/mingw/include \
  --enable-languages=c,c++ --enable-threads=win32 --disable-multilib --disable-nls --disable-werror \
  --disable-libssp --disable-libgomp --disable-libquadmath --with-dwarf2 --disable-sjlj-exceptions \
  --with-as=$P/bin/$T-as --with-ld=$P/bin/$T-ld
make -j16 MAKEINFO=true all-gcc
make MAKEINFO=true install-gcc

# 3. the C runtime (msvcrt import libs, 32-bit only)
mkdir -p $G/crt && cd $G/crt
$G/src/mingw-w64/mingw-w64-crt/configure --host=$T --prefix=$P/$T --with-sysroot=$P \
  --with-default-msvcrt=msvcrt --disable-lib64 --enable-lib32
make -j16 && make install

# 4. libgcc + libstdc++ (win32 threads)
cd $G/gcc1
make -j16 MAKEINFO=true all-target-libgcc all-target-libstdc++-v3
make MAKEINFO=true install-target-libgcc install-target-libstdc++-v3
echo "done: $P/bin/$T-g++"
