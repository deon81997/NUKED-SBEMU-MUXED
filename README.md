<div align="center">

```text
███╗   ██╗██╗   ██╗██╗  ██╗███████╗██████╗     ███████╗██████╗       ███████╗███╗   ███╗██╗   ██╗
████╗  ██║██║   ██║██║ ██╔╝██╔════╝██╔══██╗    ██╔════╝██╔══██╗      ██╔════╝████╗ ████║██║   ██║
██╔██╗ ██║██║   ██║█████╔╝ █████╗  ██║  ██║    ███████╗██████╔╝█████╗█████╗  ██╔████╔██║██║   ██║
██║╚██╗██║██║   ██║██╔═██╗ ██╔══╝  ██║  ██║    ╚════██║██╔══██╗╚════╝██╔══╝  ██║╚██╔╝██║██║   ██║
██║ ╚████║╚██████╔╝██║  ██╗███████╗██████╔╝    ███████║██████╔╝      ███████╗██║ ╚═╝ ██║╚██████╔╝
╚═╝  ╚═══╝ ╚═════╝ ╚═╝  ╚═╝╚══════╝╚═════╝     ╚══════╝╚═════╝       ╚══════╝╚═╝     ╚═╝ ╚═════╝
          ─────[ a Roland Sound Canvas for your DOS games -- no MIDI hardware needed ]─────
```

[![Version](https://img.shields.io/github/v/release/deon81997/NUKED-SBEMU-MUXED?label=latest&color=blue)](https://github.com/deon81997/NUKED-SBEMU-MUXED/releases/latest)
![Platform](https://img.shields.io/badge/runs%20on-MS--DOS%20%7C%20Windows%2098-008080)
![Home](https://img.shields.io/badge/home%20machine-Dell%20Latitude%20D810-555)
![Licence](https://img.shields.io/badge/licence-GPL--2.0--or--later-green)

<img src="docs/main-menu.png" width="720" alt="The NUKED SB-EMU main menu">

</div>

```text
╔══════════════════════════════════════════════════════════════════════════════╗
║  RELEASE ........ NUKED SB-EMU 1.0.0 (beta, -usb series)                     ║
║  TYPE ........... DOS sound pack: SB16 + OPL3 + SC-55 + MT-32 + CD-Audio     ║
║  NEEDS .......... a PC with onboard/PCI sound, plain DOS, your own ROMs      ║
║  HOME SYSTEM .... Dell Latitude D810, Pentium M, ICH6, Win98SE / DOS 7.1     ║
║  SYSOP SAYS ..... grab the zip, unzip, run SETUP, play Doom with a Canvas    ║
╚══════════════════════════════════════════════════════════════════════════════╝
```

## ░▒▓ What is this thing? ▓▒░

You know the feeling: you fire up Doom or Duke 3D on an old laptop, the music
starts, and it's... OPL FM. Fine, but you *know* how it's supposed to sound.
It's supposed to sound like a Roland Sound Canvas.

NUKED SB-EMU puts one inside your PC. It's [SBEMU](https://github.com/crazii/SBEMU)
(a Sound Blaster 16 + OPL3 emulator that runs in plain DOS on modern-ish PCI
sound chips) with a real SC-55 built in: [Nuked-SC55](https://github.com/nukeykt/Nuked-SC55)
runs the actual Roland firmware from your ROM dumps, so it's not "a General MIDI
SoundFont that sounds kind of close", it's the thing itself. Add a virtual CD
drive with CD music, USB gamepads in DOS, a Windows 98 side, and menus for all
of it, and that's the pack.

It's built and tested on a Dell Latitude D810, and most of it has been
checked in QEMU and 86Box along the way. It's still beta: if something
breaks, tell me (see [Reporting a bug](#-reporting-a-bug-)).

---

## ░▒▓ Latest news ▓▒░

```text
 DATE        VERSION   WHAT CHANGED
 ──────────  ────────  ─────────────────────────────────────────────────────────
 2026-11-09  beta.71   CUE images with CD music can be taken out and swapped
                       again (they used to stay stuck on M: until a reboot)
 2026-11-09  beta.70   Bug sweep: MIDPLAY redraws after the file browser, menu
                       text fits, Automatic setup is minutes faster on slow PCs
 2026-11-09  beta.69   SC-55 emulation ~20% faster, sample-for-sample the same
 2026-11-08  beta.68   Dark Forces' setup no longer freezes with the SC-55
```

<details>
<summary><b>The speed-up, in a bit more detail</b></summary>

The SC-55 is a little computer: a Hitachi H8 CPU running Roland's firmware,
plus a custom sound chip. Nuked-SC55 emulates all of it, every clock tick,
which is why it sounds right and why it's hungry. beta.69 cut out work that
never changes the result:

- the chip's timers are only stepped when a tick actually matters,
- wave ROM reads use a bank table instead of a chain of checks,
- the CPU loop skips the sound chip and the A/D converter when they're idle.

The output was checked against the old version sample by sample (every ROM
set, effects on and off, note limits): it's identical. It's just less work:
about 19% fewer instructions per second of music, which came out as **+24% on
a Celeron 366** (0.28× → 0.35× real time).

beta.68 also stopped the SC-55 from starving games on slow PCs: with automatic
speed control on (the default), the synth never takes more than about 60% of
the CPU, so the game always gets the rest.
</details>

---

## ░▒▓ What you get ▓▒░

```text
  [■] Sound Blaster 1.x/2.0/Pro/16 + OPL3 FM, in pure DOS, on your PCI chip
  [■] Roland SC-55 and SC-55mkII -- the real firmware, your ROMs
  [■] Roland MT-32 / CM-32L (Munt), or any .SF2 SoundFont
  [■] Virtual CD drive M: -- ISO, CUE/BIN, multi-disc, CD music mixed in
  [■] Up to two USB Xbox 360 pads as gameport joysticks, USB keyboard + mouse
  [■] Windows 98: SB/OPL3/MPU for DOS boxes + a "NUKED SB-EMU" MIDI device
  [■] Menus, game profiles, a MIDI player, tests, and a one-key bug report
```

> [!IMPORTANT]
> **ROMs are not included.** Bring your own SC-55 (and MT-32 / CM-32L) dumps.
> They're recognised by what's inside them, so the file names don't matter.

<details>
<summary><b>Sound Blaster, FM and PC speaker</b></summary>

- Sound Blaster 1.x, 2.0, Pro, Pro 2 and 16: 8- and 16-bit DMA, mono, stereo, high-speed modes, Creative ADPCM
- Any port (220 / 240), IRQ (5 / 7 / 9) and DMA; `BLASTER` is set for games automatically
- If the sound chip already sits on your chosen IRQ, SBEMU picks a free one instead of giving up
- OPL3 FM: [Nuked-OPL3-fast](https://github.com/tgies/Nuked-OPL3-fast) (cycle-accurate) or DOSBox's fast core; hardware FM pass-through on cards that have it
- PC speaker sound mixed into the output
- Protected-mode games out of the box (HDPMI32i); real-mode games with JEMMEX + QPIEMU (included) or QEMM
- 16-bit protected-mode games (Borland Pascal / C++ DPMI, e.g. Jazz Jackrabbit) with SBEMU16 + HDPMI16I (`SC55J16.BAT`)
</details>

<details>
<summary><b>MIDI: SC-55, MT-32, CM-32L, SoundFont</b></summary>

- **SC-55** (mk1 v1.00 – v2.00) and **SC-55mkII** (v1.01) firmware emulated with Nuked-SC55; ROM sets found by content, the best one picked for you
- **MT-32** and **CM-32L / LAPC-I** with Munt 2.8.5
- **SoundFont** (`.SF2`) with reverb and chorus, for slower CPUs
- MPU-401 UART on any port (330, 300, 310, 320, 332, 334); every tool finds it wherever it is
- Render-ahead buffer, timer give-back and automatic speed control keep music smooth while games run
- **Automatic setup** measures each ROM set on your CPU and picks the best one it can run
- **Front-panel overlay** in games: <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>F12</kbd> shows the SC-55 / MT-32 display over the game
- `MIDPLAY`: MIDI player with playlists, shuffle, a file browser and a live front panel
- `SC55WAV` / `SC55LOG`: render an SC-55 demo to WAV without SBEMU or a sound card, and see how fast your PC runs each ROM set
</details>

<details>
<summary><b>Virtual CD drive and CD music</b></summary>

- Mounts `.ISO`, `.CUE/.BIN` (raw or 2048-byte) and multi-disc games on drive **M:** (uCDD + SHSUCDX)
- **CD music played through the sound card**, mixed with the game's sound (`HYBRID.BAT`)
- Discs can be swapped while SBEMU runs; the music stops when one is taken out
- CD images screen: folders, disc label and date, track list with lengths, file list read without mounting, run a disc's installer, fix broken CUE files, eject
- Drivers for a **real CD-ROM drive** in DOS: XCDROM (included), VIDE-CDD, LG GSCDROM, MSCDEX
- Optional CD-ROM driver at every start (`BOOTCFG.BAT`), undone exactly by `RESET.BAT`
</details>

<details>
<summary><b>Menus and tools</b></summary>

- **MENU**: sound, settings, tests, games, CD-ROM and files in one place; shows whether SBEMU is loaded and the port, IRQ, DMA and MIDI port it really uses, and why it failed if it did
- **Games**: per-game profiles (SBEMU options, CD image, arguments)
- **SETTINGS**: every option on one screen; changes apply to a running SBEMU straight away
- **GAMETEST**: runs games unattended (Doom demos, Duke 3D with typed quit keys), measures music timing and finds each game's best settings
- Seven styles (Groovy, Classic, Sunset, Ocean, Neon, Candy, Midnight), a 3D menu (640×480, no 3D card needed), Terminus or VGA fonts, text modes from 80×25 to 80×60 plus your card's VESA modes, safe mode (hold <kbd>Shift</kbd>)
- `SBDIAG`, `MPUTEST`, `TIMETEST`, `JOYTEST`, `USBTEST`, `KMTEST`, `CPUMHZ`
- `SETUP`, `CDINST`, `CLEANUP`, `UNINSTAL`: install and remove without touching `CONFIG.SYS` or `AUTOEXEC.BAT`
</details>

<details>
<summary><b>Windows 98</b></summary>

- Virtual Sound Blaster (1.5 – 16), OPL3 and MPU-401 for DOS boxes (`SC55W.VXD`)
- **"NUKED SB-EMU" MIDI device** for Windows programs, played by the same synths
- Tray program with a settings window that matches the DOS one; settings shared with DOS
- See [`README98.TXT`](sc55-pack/pack/README98.TXT)
</details>

---

## ░▒▓ How fast a PC do I need? ▓▒░

Sound Blaster and FM run on anything with a Pentium. The SC-55 is the hungry
part, because it emulates Roland's hardware clock by clock. It needs a CPU
with CMOV (Pentium Pro / II or newer).

**Measured** with `SC55LOG` / `SC55WAV` (1.0× = exactly real time):

```text
 CPU                               SC-55 mk1 v2.00     NOTES
 ────────────────────────────────  ──────────────────  ────────────────────────
 Celeron 366 (86Box)               0.35x  (was 0.28x)  too slow; use a SoundFont
 Celeron 533 (86Box, before b.69)  0.41x               too slow; SB + FM fine
 Pentium M (Dell D810)             plays live          the home machine
```

**Estimated** from those numbers and the beta.69 speed-up. These are educated
guesses, so run *Settings > Automatic setup* on your PC for the real answer:

| SC-55 | Just about plays (≈1.0×) | Comfortable (≈1.5×, the game keeps its share) |
|---|---|---|
| **mk1** (SC-55) | Pentium III / Celeron ≈ 1 GHz, Pentium M ≈ 900 MHz, Athlon XP 1200+ | **Pentium M ≈ 1.2 GHz, Pentium 4 ≈ 2.0 GHz, Athlon XP 1600+** |
| **mkII** (SC-55mkII) | about 1.6–1.9× more than the mk1: Pentium M ≈ 1.5 GHz, Pentium 4 ≈ 2.4 GHz | Pentium M ≈ 2 GHz, Pentium 4 ≈ 3 GHz, Athlon 64 and up |

Below the "just about plays" line, the SC-55 falls behind and music drags.
Pick a SoundFont instead: it runs on a Pentium II without breaking a sweat.
Turning reverb/chorus off or limiting notes helps a little, but most of the
cost is the emulated CPU, which always runs at full speed.

| Also needed | Minimum | Nice to have |
|---|---|---|
| **Memory** | 32 MB (about 21 MB stay resident with the SC-55) | 64 MB or more |
| **Sound** | a PCI / onboard chip from the [list below](#-supported-sound-chips-) | Intel AC'97 or HD Audio |
| **DOS** | MS-DOS 6.22 / 7.x or FreeDOS, booted to plain DOS | Windows 98 "Restart in MS-DOS mode" |
| **Disk** | about 25 MB, more for disc images | |

---

## ░▒▓ Getting started ▓▒░

```text
  1. Download the release zip, unzip it, put your ROM dumps in NUKEDSB\FILES\ROMS
  2. Boot DOS          (Windows 98: "Restart in MS-DOS mode")
  3. cd NUKEDSB
     SETUP             installs to C:\SC55
     CDINST            optional: virtual CD drive
  4. C:\SC55\MENU      and off you go
```

In each game's setup pick **Sound Blaster 16** and **General MIDI / Sound
Canvas** (or **MT-32**) with the values the menu shows top right: usually 220,
IRQ 7, DMA 1/5 and port 330. GAMETEST sets Doom- and Duke-style games up by
itself.

<details>
<summary><b>More: real-mode games, CD music, settings</b></summary>

- **Protected-mode games** (DOS/4GW and friends): `C:\SC55\SC55.BAT`, then the game
- **Real-mode games**: `SC55JEM.BAT` (JEMM + QPIEMU) or `SC55QMM.BAT` (QEMM, which you supply)
- **CD music**: put the image in `C:\CDIMAGES`, mount it in the CD images screen, start with `HYBRID.BAT`
- **Settings** live in `C:\SC55\SBOPTS.TXT`, shared by every launcher and by Windows 98

The `README.TXT` in the pack is the full manual.
</details>

---

## ░▒▓ USB in DOS: Xbox 360 pads, keyboard, mouse ▓▒░

<img src="docs/joytest.png" width="560" alt="JOYTEST showing the pad's sticks and buttons">

DOS games read an analog joystick on the gameport (port 201h). SBEMU answers
there itself with a USB Xbox 360 pad: left stick = joystick A (the D-pad
too), right stick = joystick B, A/B/X/Y = buttons 1-4, Start and Back typed as
Enter and Esc. With **two pads**, pad 1 is joystick A and pad 2 joystick B.
Games that ask the BIOS for the joystick (INT 15h) get the pad too.

- **Stick types** (`/JOYMODE`): standard, CH Flightstick Pro, Thrustmaster FCS, Gravis GamePad Pro (GrIP), or native (a real gameport)
- **USB keyboard** (on by default) typed through the PC's keyboard controller, so DOS sees a PS/2 keyboard
- **USB mouse** through a DOS mouse driver: CuteMouse is included and loaded after SBEMU for you
- **Pads:** the wired Microsoft pad and wired compatibles (Hori, Mad Catz, Logitech, Saitek...). Not Xbox One / Series pads
- **Controllers:** UHCI, OHCI, EHCI and xHCI, through hubs (up to four deep), hot-plugging. Not CardBus cards
- **How:** a small polled USB host inside SBEMU (`sbemu/usbpad.c`), no USB interrupt, so it never fights the sound chip for an IRQ

Works on a real D810 (ICH6 UHCI). Hubs are tested in QEMU on every controller
type; a full-speed pad behind a USB 2.0 hub on a USB 2.0 port still wants a
real-hardware check.

---

## ░▒▓ Supported sound chips ▓▒░

Sound comes out of the PCI chip in your PC. These are all the chips the SBEMU
in this pack has a driver for, from the drivers' own PCI ID tables.
✅ = listed as tested and working; 🧪 = ported from Linux, works for some people, less tested.

### Onboard (motherboard) audio

| Family | Chips | |
|---|---|:---:|
| **Intel AC'97** (`sc_ich`) | 82801AA/AB/BA (ICH, ICH0, ICH2), ICH3, ICH4, ICH5, **ICH6** *(Dell D810)*, ICH7, ESB, ESB2, 440MX | ✅ |
| **nVidia AC'97** (`sc_ich`) | nForce, nForce2, nForce3, MCP04, CK8, CK8S, CK804 | ✅ |
| **SiS / AMD AC'97** (`sc_ich`) | SiS 7012, AMD-768, AMD-8111 | ✅ |
| **VIA AC'97** (`sc_via82`) | VT82C686, VT8233 / 8233A / 8235 / 8237 | ✅ |
| **Intel HD Audio** (`sc_inthd`) | ICH6 – ICH10, ESB2, 5 Series / 3400, Cougar Point, Patsburg, Panther Point, Lynx Point (+LP), 9 Series, Wildcat Point, Wellsburg, Lewisburg, Sunrise Point, Kaby Lake, Coffee Lake, Cannon Lake, Comet Lake, Ice Lake, Jasper Lake, Tiger Lake, Alder Lake, Elkhart Lake, Broxton, Gemini Lake, Bay Trail, Braswell, Poulsbo, Oaktrail | ✅ |
| **AMD / ATI HD Audio** (`sc_inthd`) | ATI SB450, SB600, AMD Hudson, X370-class, X570-class, Stoney, Raven | ✅ |
| **nVidia HD Audio** (`sc_inthd`) | MCP51, MCP55, MCP61, MCP65, MCP67, MCP73, MCP77, MCP79, MCP89 | ✅ |
| **Other HD Audio** (`sc_inthd`) | VIA 82xx / VT7122 / VT6122, SiS 966, ULi M5461, Zhaoxin, C-Media HDA, Vortex86MX, Teradici | ✅ |
| **HDMI audio** (`sc_inthd`) | Intel Haswell / Broadwell HDMI, ATI/AMD Radeon HDMI (RS600 – R6xxx and later) | 🧪 |
| **Geode** (`sc_cs5535`) | National CS5535, AMD CS5536 | 🧪 |

### Sound cards

| Family | Cards / chips | |
|---|---|:---:|
| **Creative SB Live!** (`sc_sbliv`) | EMU10K1: Live!, Live! Value, Live! 5.1, Live! Platinum, PCI512 (CT4620 – SB0220) | ✅ |
| **Creative Audigy** (`sc_sbliv`) | EMU10K2: Audigy 1 / ES, Audigy 2 / ZS / EX / Platinum / Value, Audigy 4 / 4 Pro | ✅ |
| **Creative Audigy LS / Live! 24-bit** (`sc_sbl24`) | CA0106: Audigy LS, Live! 7.1 24-bit, Live24 OEM, X-Fi Xtreme Audio (SB0790) | ✅ |
| **Creative / Dell SB0200** (`sc_emu10k1x`) | EMU10K1X | 🧪 |
| **Creative X-Fi** (`sc_ctxfi`) | EMU20K1, EMU20K2 | 🧪 |
| **Ensoniq / Creative** (`sc_e1371`) | **ES1371**, ES1373, CT5880, Ectiva EV1938 (SB PCI 64/128) | ✅ |
| **Ensoniq AudioPCI** (`sc_e1370`) | ES1370 | 🧪 |
| **C-Media** (`sc_cmi`) | CMI8338A, CMI8338B, CMI8738, CMI8738B | ✅ |
| **C-Media Oxygen** (`sc_oxygen`) | CMI8786/8787/8788: **Asus Xonar DG / DGX**, HT-Omega Claro / Claro halo, AuzenTech X-Meridian / 2G, TempoTec HiFier Fantasia / Serenade, Kuroutoshikou CMI8787, C-Media reference boards | 🧪 |
| **Yamaha DS-XG** (`sc_ymf`) | YMF724, YMF724F, YMF740, YMF740C, **YMF744**, **YMF754** | 🧪 |
| **ESS** (`sc_allegro`) | Maestro 2 (ES1968), Maestro 2E (ES1978), Allegro / Allegro-1 (ES1988S/ES1989S), Maestro-3i (ES1983), Canyon3D 2 / LE (ES1990/1992) | 🧪 |
| **Avance Logic** (`sc_als4000`) | ALS4000 | 🧪 |
| **Trident / SiS** (`sc_trident`) | 4DWave DX, 4DWave NX, SiS 7018 | 🧪 |
| **Cirrus Logic** (`sc_cs4281`) | CS4281 | 🧪 |

> [!TIP]
> Not sure what your PC has? Boot DOS and run `SBEMU` once: it prints the chip it found.
> In emulators, QEMU's `AC97` and `intel-hda` both work.

---

## ░▒▓ Screenshots ▓▒░

<table>
<tr>
<td align="center"><img src="docs/cd-images-groovy.png" width="400"><br><sub>CD images, <i>Groovy</i> style</sub></td>
<td align="center"><img src="docs/cd-images-classic.png" width="400"><br><sub>Same screen, <i>Classic</i> style</sub></td>
</tr>
<tr>
<td align="center"><img src="docs/cd-files.png" width="400"><br><sub>Files on a disc image, read without mounting it</sub></td>
<td align="center"><img src="docs/win98-settings.png" width="400"><br><sub>Windows 98 settings, shared with DOS</sub></td>
</tr>
</table>

---

## ░▒▓ Downloads ▓▒░

**[>> Grab the latest release zip <<](https://github.com/deon81997/NUKED-SBEMU-MUXED/releases/latest)**:
the DOS pack plus the Windows 98 part, ready to install. Source is this repo
(GPL-2.0-or-later).

### What you bring yourself

The zip leaves out what it can't legally share. For the SC-55 you only need the ROMs.

| What | For | Where to get it | Goes in |
|---|---|---|---|
| **Roland SC-55 / SC-55mkII ROMs** | the SC-55 | Copyrighted: **dump them from a unit you own**. Not needed with a SoundFont | `FILES\ROMS` |
| **Roland MT-32 / CM-32L ROMs** | the MT-32 (optional) | Dump them from a unit you own | `FILES\ROMS` |
| **A SoundFont** (`.SF2`) | the SoundFont synth (optional) | Any General MIDI SoundFont; plenty are free | `C:\SC55\SF2` |
| **MSCDEX.EXE** (Microsoft) | a real CD-ROM drive (optional) | Comes with Windows 98: SETUP picks it up by itself | automatic |
| **VIDE-CDD.SYS** (Acer / BenQ) | another CD-ROM driver (optional; XCDROM is included) | Acer's `APICD214.EXE`: [Vogons wiki](https://www.vogonswiki.com/index.php/VIDE-CDD.SYS), [DOS Days](https://www.dosdays.co.uk/topics/Software/optical_downloads.php) | `FILES\VIDECD` |
| **GSCDROM.SYS** (LG) | LG drives XCDROM doesn't like (optional) | [DOS Days](https://www.dosdays.co.uk/topics/Software/optical_downloads.php) | `FILES\GSCDROM` |
| **QEMM 8 / 9** | `SC55QMM.BAT` only (optional) | Out of print; JEMMEX + QPIEMU (included) does the same job | `FILES\QEMM` |

---

## ░▒▓ Repository layout ▓▒░

```text
sc55-pack/
├── main.c, sbemu/, mpxplay/, drivers/   SBEMU + sound-chip drivers, with the pack's additions
│   └── sbemu/vmpu_sc55.c, vmpu_mt32.cpp, vmpu_sf.c, cdlink.c, usbpad.c
├── sc55/                 SC-55 engine around Nuked-SC55
├── upstream/Nuked-SC55   Nuked-SC55 backend (jcmoyer fork) with this pack's bit-exact speed-ups
├── third_party/          Munt mt32emu 2.8.5, Nuked OPL3-fast 1.8-fast.3
├── tools/                DOS programs (menu, settings, GAMETEST, MIDPLAY, CD images...) + text UI
├── win98/                Windows 98 part: vxd/, helper/, mididrv/, compat/
├── pack/                 the pack's batch files and guides
├── scripts/              release helpers (incl. the uCDD unmount fix)
├── testrig/              QEMU / 86Box test machines (disk builder, VM driver, fake USB pad)
└── tests/                host-side engine tests
```

[`HANDOFF.md`](sc55-pack/HANDOFF.md) is the running dev log: what changed, how it
was tested, what still needs real hardware.

### Building

```sh
export PATH=/path/to/djgpp/bin:$PATH   # i586-pc-msdosdjgpp-gcc (GCC 12)
cd sc55-pack
make -j2                               # -> output/
```

`make VSC55=0` builds plain SBEMU. The Windows 98 part (`win98/build98.sh`) needs
MinGW-w64 i686, Open Watcom 2 and JWasm; see [`win98/SOURCES.TXT`](sc55-pack/win98/SOURCES.TXT).

---

## ░▒▓ Reporting a bug ▓▒░

In DOS, open **MENU → Files → Make a bug report** (or run `BUGREP` in `C:\SC55`).
It writes `C:\SC55\BUGREP.TXT` with your config files, memory, settings,
SBEMU's messages, its start-up details and test results. Attach that to an
[issue](https://github.com/deon81997/NUKED-SBEMU-MUXED/issues) and tell me what
you were doing. If SBEMU hung, the last `[step]` line in `SBDETAIL.TXT` shows
exactly where.

---

## ░▒▓ Greetz & credits ▓▒░

None of this exists without the people below. Thank you all.
The combined work is **GPL-2.0-or-later** ([`COPYING`](sc55-pack/COPYING)).

### Built into SBEMU.EXE

| Part | Authors | Licence |
|---|---|---|
| [SBEMU](https://github.com/crazii/SBEMU) | **crazii** and SBEMU's contributors | GPL-2.0+ |
| Sound-chip drivers from [MPXPlay](https://mpxplay.sourceforge.net/) | **Attila Padar** (PDSoft) | open source, used with the author's permission ([note](sc55-pack/mpxplay/au_cards/LICENSE)) |
| Additional drivers ported from Linux ALSA | **[jiyunomegami](https://github.com/jiyunomegami)**, after the Linux ALSA authors | GPL |
| [Nuked-SC55](https://github.com/nukeykt/Nuked-SC55) | **nukeykt (Nuke.YKT)** | GPL-2.0+ |
| Nuked-SC55 [backend fork](https://github.com/jcmoyer/Nuked-SC55) | **J.C. Moyer (jcmoyer)**: library backend, fast H8/532 VM, ROM detection by hash | GPL-2.0+ |
| Nuked-SC55 research and help | John McMaster (PCM chip decap), org / ogamespec (deroute tool), Wohlstand, mattw, HardWareMan, giulioz, Cloudschatze, NikitaLita, Karmeck, Falcosoft, Michael Mouton | — |
| [Munt](https://github.com/munt/munt) libmt32emu 2.8.5 | **Dean Beeler, Jerome Fisher, Sergey V. Mikayev** and the Munt team | LGPL-2.1+ |
| [Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3) / [Nuked-OPL3-fast](https://github.com/tgies/Nuked-OPL3-fast) | **nukeykt**; fast fork by **Tony Gies (tgies)** | LGPL-2.1+ |
| DOSBox OPL (DBOPL) and PC speaker | **The DOSBox Team**; PC speaker port to SBEMU by [andersrodrig](https://github.com/andersrodrig) | GPL-2.0+ |
| [TinySoundFont](https://github.com/schellingb/TinySoundFont) | **Bernhard Schelling**, after SFZero by Steve Folta | MIT |
| SoundFont MPU-401 in SBEMU | **[Cacodemon345](https://github.com/Cacodemon345/VSBHDASF)** (VSBHDASF) | — |
| USB host for pads, keyboard and mouse | written for this pack; UHCI / OHCI after [USBDDOS](https://github.com/crazii/USBDDOS) (**crazii**), xHCI bring-up after [xhci98](https://github.com/yeokm1/xhci98)'s DOS tool (**Yeo Kheng Meng**), pad layout after Linux `xpad.c` | GPL-2.0 |
| Terminus font | **Dimitar Zhekov** | OFL-1.1 |

### Shipped in the release zip

| Program | Authors | Licence |
|---|---|---|
| HDPMI32i, HDPMI16I | **Japheth (Andreas Grech)** ([HX](https://github.com/Baron-von-Riedesel/HX)); port-trap API and IOPL0 build by **crazii** ([crazii/HX](https://github.com/crazii/HX)); HDPMI16I built from crazii/HX with a two-line patch ([notes](sc55-pack/third_party/hdpmi16/README.TXT)) | freeware, source available |
| JEMMEX, JEMM386, JLOAD, QPIEMU | **Japheth** ([Jemm](https://github.com/Baron-von-Riedesel/Jemm)) | see `JEMMLIC.TXT` |
| μCDD 0.9.5-a2 | **[vorvek](https://github.com/vorvek/uCDD)**; this copy adds a CD-audio link for SBEMU and the beta.71 unmount fix ([`scripts/ucdd-unmount-fix.py`](sc55-pack/scripts/ucdd-unmount-fix.py)) | GPL-3.0 |
| SHSUCDX | **Jason Hood** ([shsucd](https://github.com/adoxa/shsucd)), from v1.4b by **John McCoy** | freeware, source available |
| XCDROM | **Jack R. Ellis** | GPL, source in the zip |
| [CuteMouse](https://cutemouse.sourceforge.net/) 2.1b4 | **Nagy Daniel**, **Arkady V. Belousov**, 2.1 betas by **Eric Auer**, and contributors | GPL-2.0, source in the zip |

Not shipped, belonging to their owners: MSCDEX and MOUSE.COM (Microsoft), VIDE-CDD (Acer / BenQ), GSCDROM (LG), QEMM (Quarterdeck).

Built with [DJGPP](https://www.delorie.com/djgpp/) (DJ Delorie) via [build-djgpp](https://github.com/andrewwutw/build-djgpp),
[MinGW-w64](https://www.mingw-w64.org/), [Open Watcom 2](https://github.com/open-watcom/open-watcom-v2) and
[JWasm](https://github.com/Baron-von-Riedesel/JWasm); tested in [QEMU](https://www.qemu.org/) and [86Box](https://86box.net/).

```text
 ──────────────────────────────────────────────────────────────────────────────
  NUKED SB-EMU  ·  made for fun on a Dell D810  ·  bring your own ROMs  ·  EOF
 ──────────────────────────────────────────────────────────────────────────────
```

<sub>Roland, SC-55, MT-32 and CM-32L are trademarks of Roland Corporation. Sound Blaster is a trademark of Creative Technology. Xbox is a trademark of Microsoft. NUKED SB-EMU is not affiliated with any of them.</sub>
