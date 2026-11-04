#!/bin/sh
# native (Linux) build of the helper engine + test, for checking the audio
set -e
N=/home/claude/nsc55j; M=/home/claude/munt/mt32emu; S=/home/claude/sbemu-src/sbemu; O=/home/claude/nopl3
CXX="g++ -std=c++23 -O2 -I$N/build98/backend -I$N/src -I$N/src/backend -I$M/b98 -I$M/src -I$S -I$O"
mkdir -p be mt
[ -f be/.done ] || { ls $N/src/backend/*.cpp $N/src/backend/decoder2/*.cpp | xargs -P2 -I{} sh -c "$CXX -c {} -o be/\$(basename {} .cpp).o"; gcc -O2 -c $N/src/backend/sha/sha224-256.c -o be/sha.o; touch be/.done; }
[ -f mt/.done ] || { ls $M/src/*.cpp $M/src/sha1/*.cpp | grep -v VersionTagging | xargs -P2 -I{} sh -c "g++ -std=c++11 -O2 -DMT32EMU_EXPORTS_TYPE=3 -I$M/b98 -I$M/src -c {} -o mt/\$(basename {} .cpp).o"; touch mt/.done; }
H=/home/claude/sc55w98/helper
for f in opts romscan synths engine; do $CXX -c $H/$f.cpp -o $f.o; done
g++ -std=c++17 -O2 -c $S/dbopl.cpp -o dbopl.o
gcc -O2 -c $O/opl3.c -o opl3.o
$CXX -c /home/claude/sc55w98/test/enginetest.cpp -o enginetest.o
g++ -o enginetest enginetest.o opts.o romscan.o synths.o engine.o dbopl.o opl3.o be/*.o mt/*.o -lpthread
