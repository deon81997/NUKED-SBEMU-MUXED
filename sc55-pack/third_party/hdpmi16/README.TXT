HDPMI16I.EXE for SBEMU16
========================

SBEMU16.COM (tools/sbemu16.asm) gives 16-bit protected-mode games (Borland Pascal/C++ "16-bit DPMI"
programs such as Jazz Jackrabbit) Sound Blaster, OPL, MPU-401 and joystick through SBEMU. It needs a
16-bit HDPMI that has SBEMU's port-trap API (INT 2Fh 168Ah "HDPMI", AX=6/7/8), which only crazii's HX
fork has, and that fork ships no 16-bit binary. HDPMI16I.EXE in the release zip is built from:

  https://github.com/crazii/HX  commit 43182b4a0efd008ae5aab3ccfcc6d644437cd111
  + hdpmi16.diff (two lines):
    - HDPMI.INC: the IRQ routing that comes with ?PMIOPL=0 is 32-bit only code -> off for 16-bit
    - I2FHDPMI.ASM: 16-bit keeps the trap frame on the client's stack (?USE_HOST_MEM 0), so a trap
      taken inside a game's IRQ handler while another trapped access is in progress works
  built with JWasm 2.21 (0a1008d) / JWlink (b930c84), Baron-von-Riedesel, by build.sh: ?32BIT=0, ?PMIOPL=0, ?VIODIROUT=1.

HDPMI16 and HDPMI32 only stay resident together when they are the same build, so the pack's
HDPMI32I.EXE (crazii v0.1-beta4fix2) must be used with it; load HDPMI16I -r first.

HX is Japheth's (Andreas Grech) freeware DOS extender; the port-trap additions are crazii's.
