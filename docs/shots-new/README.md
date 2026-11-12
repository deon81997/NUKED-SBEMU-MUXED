# SSF2T / 32RTM investigation screenshots (86Box P2B-LS, ES1371 on IRQ 12, 2026-11-11)

HDPMI builds: H1 = crazii HX 43182b4 + mov-cr0.diff; H2 = H1 with ?IRQ_ROUTE_IVT=0; H3 = ?TRAP_CLISTI=0;
H4 = ?SKIP_CLISTI=0; H5 = H1 + pushad/popad around the real-mode IRQ call. SBEMU = beta.91.

- s1: H1 + SBEMU, SF2TURBO: 32RTM exception 0D in the decompressor (`rep movsb`, bad ESI/ECX).
- s2: H1 without SBEMU: game stops at "Unable to Initialise Sound Driver" (text screen, image is mostly blank).
- s3: H1 + SBEMU /JOY0 /USBMS0 /USBKB0 /VTIMER0: exception 0D in the clear-screen `rep stosd`, ECX=001DEB46 (should be <=3E80h).
- s4: H2, same crash (IRQ_ROUTE_IVT not the cause).
- s5: SBEMU /PM0: sound init fails before the crash point.
- s6: H3: hangs while SBEMU loads (TF trick for POPF is needed).
- s7: H4, same crash, ECX=001DEB60.
- s8: H5, same crash (real-mode IRQ handlers not the cause).
- s9: H1 + SBEMU with 86Box dynarec off: crash again in the decompressor, ECX=001CF8F8 (not an emulator bug).
- i1-i10: the game's INSTALL.EXE menus (attempt to select No Sound; the setting did not save).

MAKESWP itself exited normally in every run on this machine (no exit hang).
