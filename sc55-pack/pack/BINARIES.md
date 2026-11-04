# Binaries not in this repository

The release zip (`NUKED-SB-EMU.zip`) contains programs that are not stored here:

- Built from this repository (`make`, DJGPP cross compiler): `SBEMU.EXE`, `SC55MENU.EXE`, `SBSET.EXE`, `GAMETEST.EXE`, `SBDIAG.EXE`, `MIDPLAY.EXE`, `MPUTEST.EXE`, `SC55WAV.EXE`, `SC55LOG.EXE`, `TIMETEST.EXE`, `CDGUI.EXE`, `CPUMHZ.EXE`.
- Third-party DOS programs, each from its own project: HDPMI32I (HX), JEMM/JLOAD/QPIEMU (Baron-von-Riedesel), uCDD, SHSUCDX, VIDE-CDD, XCDROM (source in `FILES/XCDROM`), GSCDROM, MSCDEX (Microsoft), the QEMM files (your own copy).
- ROM dumps (`FILES/ROMS`): never stored here. Use your own SC-55 / MT-32 dumps.

`FILES/CDSET/CDCHK.COM` is ours: 13 bytes, `mov ax,1500h / xor bx,bx / int 2Fh / mov al,bl / mov ah,4Ch / int 21h` (errorlevel = CD drives MSCDEX serves).
