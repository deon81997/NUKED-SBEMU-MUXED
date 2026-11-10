# sc55bench - host benchmark of our Nuked-SC55 fork

Renders a MIDI file through `sc55/sc55synth.cpp` (the same C API SBEMU uses), times it and prints an FNV hash of
every output sample, so a speed-up can be proven bit-exact (same hash before and after).

    ./build.sh                      # -O3 -march=native (MARCH=-march=x86-64-v2 for valgrind)
    ./bench ROMDIR song.mid 30      # [seconds] [romset, default auto]

Profile: `MARCH=-march=x86-64-v2 ./build.sh -g; valgrind --tool=callgrind ./bench ROMDIR song.mid 4`.

2026-11-10, mk1-v2.00, 19.mid, 30 s: ~11x real time on the session host, hash 1317ac59.
Callgrind (4 s): PCM_Update 48%, TIMER_AdvanceSel 10%, MCU_Step 9%, MCU_Read 7%, calc_tv 4%.
PGO (-fprofile-generate / -fprofile-use) on the host: +8-10%, same hash - not used for DOS yet (DJGPP GCC 12
would need its own .gcda files from a DOS run).
