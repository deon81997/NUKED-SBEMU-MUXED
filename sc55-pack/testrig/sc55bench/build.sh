#!/bin/bash
# build.sh [extra flags]: host bench of the SC-55 core as the pack builds it (O3, no fast-math)
S=$(cd "$(dirname "$0")/../.." && pwd); C=$S/upstream/Nuked-SC55/src/backend
FL="-std=gnu++20 -O3 ${MARCH:--march=native} -fomit-frame-pointer -fno-rtti -DNDEBUG -I$S/sc55/config -I$S/sc55 -I$C -include $S/sc55/shim/sc55_compat.h $*"
g++ $FL -o bench bench.cpp $S/sc55/sc55synth.cpp $C/*.cpp -x c $C/sha/sha224-256.c 2>&1 | grep -E 'error' | head
