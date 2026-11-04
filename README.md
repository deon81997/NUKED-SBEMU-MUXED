# SC-55 PACK (SC55PACK-HYBRID)

A Roland SC-55 for DOS games, on a laptop with no MIDI hardware.

The pack turns a **Dell Latitude D810** (Pentium M, AC'97 sound, Windows 98 /
MS-DOS 7.1) into a period-correct DOS sound setup:

- **Sound Blaster 16 + OPL3 + MPU-401** emulated in pure DOS by
  [SBEMU](https://github.com/crazii/SBEMU) on the laptop's PCI sound chip.
- **MIDI music played by a real SC-55 firmware**:
  [Nuked-SC55](https://github.com/nukeykt/Nuked-SC55) runs the SC-55 / SC-55mkII
  ROMs inside SBEMU. A Roland MT-32 / CM-32L ([Munt](https://github.com/munt/munt))
  or any SoundFont can be used instead.
- **A virtual CD drive** (M:) for `.CUE/.BIN`, `.ISO` and multi-disc images, with
  the game's **CD-Audio mixed into the sound** through SBEMU.
- **Menus for everything**: start the SC-55, change settings, test games, play
  MIDI files, mount disc images, all from text-mode screens with themes, fonts
  and display modes.
- **Inside Windows 98 too**: the same Sound Blaster / OPL3 / MPU-401 for DOS
  boxes, and the SC-55 as a Windows MIDI device ("SC-55 PACK"), with the same
  settings file.

It works on other PCs with a sound card SBEMU supports, but the D810 is what
it is tuned and tested for.

> **ROMs are not included.** You need your own SC-55 (and, for the MT-32,
> MT-32 / CM-32L) ROM dumps. They are found by their contents, so the file names
> don't matter.

Current version: **1.0.0-beta.35-sc55** (see `sc55-pack/HANDOFF.md` for the
state of things and what still needs testing on the D810).

---

## Contents

- [What's in the pack](#whats-in-the-pack)
- [Installing on the laptop](#installing-on-the-laptop)
- [Using it](#using-it)
- [Windows 98](#windows-98)
- [Repository layout](#repository-layout)
- [Building](#building)
- [Testing](#testing)
- [Credits and licences](#credits-and-licences)

---

## What's in the pack

| In DOS | What it does |
|---|---|
| `SBEMU.EXE` | SBEMU with the SC-55 / MT-32 / SoundFont MIDI synth built in, CD-Audio mixing, diagnostics, an in-game LCD overlay (Ctrl+Alt+F12) |
| `MENU.BAT` / `SC55MENU.EXE` | One menu for everything: sound, games (with per-game profiles and CD images), CD-ROM, settings, tools |
| `SETTINGS.BAT` / `SBSET.EXE` | Every SBEMU option on one screen, plus an automatic setup that benchmarks the ROM sets and picks the best one the CPU can run |
| `GAMETEST.EXE` | Runs your games unattended with SBDIAG and finds the SBEMU settings that give each one the best music timing |
| `MIDPLAY.EXE` | MIDI file player with a live SC-55 / MT-32 front panel |
| `SBDIAG.EXE`, `MPUTEST.EXE`, `TIMETEST.EXE` | Diagnostics: SBEMU statistics, a test tune, timer checks |
| `SC55WAV.EXE` / `SC55LOG.EXE` | ROM check and speed benchmark (writes a WAV / a report) |
| `CDSET\GUI.BAT` / `CDGUI.EXE` | The CD images screen: mount images on drive M:, run a disc's installer, fix CUE files |

Third-party DOS programs that come with the pack zip (not stored in this
repository, see `sc55-pack/pack/BINARIES.md`): HDPMI32i, JEMM / QPIEMU, uCDD,
SHSUCDX and CD-ROM drivers.

## Installing on the laptop

1. Download the pack zip (`SC55PACK-HYBRID.zip`) and put your ROM dumps in
   `FILES\ROMS`.
2. In DOS (Windows 98: *Restart in MS-DOS mode*), go to the pack folder and run
   `SETUP` (installs to `C:\SC55`).
3. Optional: `CDINST` for the virtual CD drive and the CD images screen.
4. Run `C:\SC55\MENU.BAT`.

`README.TXT` in the pack is the complete guide: every option, every screen,
real-mode games (JEMM+QPIEMU or QEMM), the virtual CD drive and
troubleshooting.

## Using it

- **Protected-mode games** (DOS/4GW and the like): `C:\SC55\SC55.BAT`, then
  start the game. In its setup choose Sound Blaster 16 (220, IRQ 7, DMA 1/5 by
  default) and General MIDI / Sound Canvas at port 330.
- **Real-mode games**: the JEMM or QEMM launchers (`SC55JEM.BAT` /
  `SC55QMM.BAT`); you supply QPIEMU or QEMM.
- **Games with CD music**: put the disc image in `C:\CDIMAGES`, mount it from
  the CD images screen, and start with `HYBRID.BAT`.
- **Settings** are saved in `C:\SC55\SBOPTS.TXT`, shared by every launcher and
  by the Windows 98 part.

## Windows 98

`SETUP98.BAT` (in the pack zip) installs the Windows part into `C:\SC55\WIN98`:

- `SC55W.VXD`: a virtual Sound Blaster (1.5 to 16), OPL3 and MPU-401 for DOS
  boxes.
- `SC55W.EXE`: makes the sound with the same synths (SC-55, MT-32, SoundFont,
  DOSBox or Nuked OPL3). It lives in the tray, with a settings window that matches
  `SBSET`, and also installs / uninstalls.
- `SC55MIDI.DRV`: the Windows MIDI device "SC-55 PACK".

See `README98.TXT` in the pack. The source is in `sc55-pack/win98/`.

## Repository layout

Everything lives in `sc55-pack/`:

| Folder | |
|---|---|
| `main.c`, `sbemu/`, `mpxplay/`, `drivers/` | SBEMU (sound card drivers, SB/OPL/MPU emulation) with the pack's additions: `sbemu/vmpu_sc55.c` (SC-55 MPU), `sbemu/vmpu_mt32.cpp` (Munt), `sbemu/vmpu_sf.c` (SoundFont), `sbemu/cdlink.c` (CD-Audio), `sbemu/vdiag.c` (diagnostics, key player, overlay) |
| `sc55/` | The SC-55 engine around Nuked-SC55 (`sc55synth.cpp`, shims) |
| `upstream/Nuked-SC55` | Nuked-SC55, unmodified |
| `third_party/` | Munt mt32emu 2.8.3, Nuked OPL3 |
| `tools/` | The DOS programs: `sc55menu.c`, `sbset.c`, `gametest.c`, `midplay.c`, `cdgui.c`, `sbdiag.c`, `mputest.c`, `timetest.c`, `sc55wav.cpp`; the shared text UI `tui.h`, `theme.h`, `fontdata.h` |
| `pack/` | The pack's batch files, README and folder layout (binaries and ROMs are added when the zip is made) |
| `win98/` | The Windows 98 part: `vxd/` (SC55W.VXD), `helper/` (SC55W.EXE: engine, synths, settings, setup), `mididrv/` (SC55MIDI.DRV), `common/`, `compat/` (Windows 98 replacements for newer DLL imports), `tools/`, `test/` |
| `testrig/` | QEMU test machine: FreeDOS disk builder, ISO builder, VM driver, key player |
| `tests/` | Host-side tests of the engine (`make -f tests/Makefile.host`) |

`README-SBEMU.md` is SBEMU's own README (supported sound cards, all SBEMU
options); `README-VSC55.md` describes the SC-55 additions to SBEMU.

## Building

Needs the DJGPP cross compiler (`i586-pc-msdosdjgpp-gcc`, GCC 12):

```sh
export PATH=/path/to/djgpp/bin:$PATH
cd sc55-pack
make -j2            # everything goes to output/
```

After changing a header, delete `output/*.o` first. `make VSC55=0` builds plain
SBEMU without the SC-55.

The Windows 98 part (`win98/build98.sh`) is built with MinGW-w64 (i686, GCC 13),
Open Watcom 2 and JWasm, against Nuked-SC55 (jcmoyer fork) and Munt; see
`win98/SOURCES.TXT` for the versions and paths.

## Testing

`sc55-pack/testrig/` runs a DOS PC in QEMU (AC'97 like the D810) and drives it
by script: build a FreeDOS disk from a folder (`mkdisk.py`), boot it
(`vm.py start`), send keys and take screenshots (`vm.py key` / `vm.py shot`).
Emulators are not the real thing: what matters is the D810, and
`HANDOFF.md` lists what still has to be checked there.

## Credits and licences

The combined work is **GPL-2.0-or-later** (`sc55-pack/COPYING`).

- SBEMU: crazii and contributors (GPL-2.0-or-later), based on MPXPlay.
- Nuked-SC55: nukeykt and contributors.
- Munt mt32emu: the Munt team (LGPL-2.1-or-later).
- Nuked OPL3: nukeykt (LGPL-2.1-or-later); DOSBox OPL: the DOSBox team.
- TinySoundFont: Bernhard Schelling (MIT).
- Terminus font: Dimitar Zhekov (OFL).
- Third-party DOS programs in the pack zip keep their own licences.

Roland, SC-55, MT-32 and CM-32L are trademarks of Roland Corporation. This
project is not affiliated with Roland.
