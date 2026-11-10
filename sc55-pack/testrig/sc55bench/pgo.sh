#!/bin/bash
# pgo.sh - how sc55/pgo/*.gcda were made (profile-guided build of the SC-55 core, beta.89). Run from sc55-pack.
# 1. instrumented SC55WAV in a copy of the tree (same flags + -fprofile-generate):
#      tar --exclude=output -cf - . | (mkdir -p /tmp/pgotree && tar -C /tmp/pgotree -xf -)
#      cd /tmp/pgotree && make SC55PGO=gen output/sc55wav.exe   (link fails: rerun its link line with
#      "-fprofile-generate -lstdc++ -lgcov")
# 2. run it in DOS (86Box): HDPMI32I, then
#      SET GCOV_PREFIX=C:/PGO
#      SET GCOV_PREFIX_STRIP=20          (keeps just the file name; DOS cuts it to 8.3: MCU_TIME.GCD ...)
#      SC55WAV ROMS C:\OUT.WAV /N
# 3. copy C:\PGO\*.GCD back and rename to what -fprofile-use + -fprofile-prefix-path expect:
#      PCM.GCD -> sc55/pgo/output#upstream#Nuked-SC55#src#backend#pcm.gcda, SC55SYNT.GCD -> output#sc55#sc55synth.gcda
#      (8 first letters of each backend .cpp name; see the beta.89 HANDOFF section)
# 4. make (SC55PGO=use is the default while sc55/pgo has .gcda files). Check: SC55WAV OUT.WAV must be bit-identical.
# Result 2026-11-10 (86Box PII-450, mk1-v2.00): 18000 -> 20456 frames/s (+13.6%), OUT.WAV md5 7355ccee... both.
# A changed source file builds without its profile (-Wno-coverage-mismatch); redo the profile after core changes.
echo "see the comments in $0"
