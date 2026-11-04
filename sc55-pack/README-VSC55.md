# VSC-55 for DOS (SBEMU + Nuked-SC55)

A DOS port of the VSC-55 idea: a Roland SC-55 / SC-55mkII software synthesizer that
DOS games reach through an **MPU-401 port trap (330h)**. It is built into
[SBEMU](https://github.com/crazii/SBEMU) 1.0.0-beta7rc1, which already provides Sound Blaster 16 and
OPL3 emulation on PCI sound cards; the FM core of VOPL3 (Nuked-OPL3) is available as an option.

> This page describes the SC-55 additions as first written. The pack has grown since (MT-32, menus,
> virtual CD, Windows 98): see [../README.md](../README.md) and [HANDOFF.md](HANDOFF.md) for the current state.

## You must supply
* Your own SC-55 / SC-55mkII ROM dumps (not included). Files are recognised by SHA-256, names don't matter.
* A PC with a supported PCI sound card (as for SBEMU), HDPMI32i, and JEMM+QPIEMU or QEMM for real-mode games.
* A fast CPU. The emulation needs roughly 1.5x real time or better; **run `SC55WAV` first** to measure.
* About 32 MB of extended memory (about 21 MB stays resident).

## Usage
```
SC55WAV roms demo.wav /V     (check ROMs boot, benchmark: speed graph in text and VGA, WAV output)
SBEMU /VMPU /VSCROM:roms       (resident; DOS games then see an MPU-401 at 330h)
SC55LOG roms                   (same benchmark, but also saves the full report to SC55LOG.TXT next to the exe)
MPUTEST                        (plays a test tune through the running SC-55; SBEMU must be loaded)
```
| Option | Meaning |
|---|---|
| `/VMPU` | enable the SC-55 MPU-401 (automatic when the ROM folder exists and has files in it; `/VMPU=0` turns that off) |
| `/VSCROM:dir` | ROM folder (default `roms`); naming it implies `/VMPU` |
| `/VSCSET:name` | ROM set, default `auto`; list is printed by `/?` |
| `/VSCGAIN:pct` | output gain 0-1600, default 100 (changeable from a second run) |
| `/FMCORE:0\|1` | OPL3 core: 0 = DOSBox DBOPL (default, fast), 1 = Nuked OPL3 (accurate, heavier) |

ROMs are read once at start-up, before going resident; the ROM set cannot be changed without unloading.

## Limitations
* MPU-401 **UART mode only** (what games use); the intelligent-mode sequencer isn't emulated.
* If the PC is slower than real time, the MIDI queue fills and games that poll the MPU status will stall.
* The Win98 GUI front panel, Win16 MIDI driver and Win32 host were not ported; the Windows faceplate artwork is not used.
* Untested: the resident-memory layout, the interrupt-time rendering speed, real hardware.

## Building
DJGPP cross or native: `make` (set `VSC55=0` for the original soundfont build). Host tests: `make -f tests/Makefile.host`.
`upstream/Nuked-SC55` is the vendored, unmodified core (Nuked-SC55 0.7.0), built with small shims in `sc55/shim`.

## Credits and licence
Combined work: **GPL-2.0-or-later**. SBEMU (crazii), Nuked-SC55 (nukeykt, Nuked-SC55 contributors),
VSC-55 and VOPL3 (original authors), Nuked-OPL3 (nukeykt; LGPL-2.1+, `third_party/nuked-opl3-fast`).
Roland, SC-55 are trademarks of their owners.

## Real-mode games (DOSMid, most DOS games): JEMM + QPIEMU
Real-mode programs can only reach the emulated MPU-401 through a port trap. Typical setup (from the SBEMU docs):
```
CONFIG.SYS      DEVICE=C:\JEMM\JEMMEX.EXE X2MAX=8192 NOEMS     (replaces HIMEM.SYS / EMM386.EXE)
AUTOEXEC.BAT    C:\JEMM\JLOAD.EXE C:\JEMM\QPIEMU.DLL
                C:\HDPMI\HDPMI32I.EXE -r -x
                C:\SBEMU\SBEMU.EXE   (or CALL C:\SC55\SC55.BAT)
```
QPIEMU.DLL is loaded with JLOAD from AUTOEXEC.BAT, not as a DEVICE line. Jemm: https://github.com/Baron-von-Riedesel/Jemm
