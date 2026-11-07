<div align="center">

# 🎹 NUKED SB-EMU

### A Roland Sound Canvas for DOS games, with no MIDI hardware at all

**SBEMU** &nbsp;•&nbsp; **Nuked-SC55** &nbsp;•&nbsp; **Munt MT-32** &nbsp;•&nbsp; **SoundFont** &nbsp;•&nbsp; **Virtual CD + CD-Audio** &nbsp;•&nbsp; **Windows 98**

[![Version](https://img.shields.io/github/v/release/deon81997/NUKED-SBEMU-MUXED?label=version&color=blue)](https://github.com/deon81997/NUKED-SBEMU-MUXED/releases/latest)
![Platform](https://img.shields.io/badge/platform-MS--DOS%20%7C%20Windows%2098-008080)
![Target](https://img.shields.io/badge/tuned%20for-Dell%20Latitude%20D810-555)
![Licence](https://img.shields.io/badge/licence-GPL--2.0--or--later-green)
![Compiler](https://img.shields.io/badge/built%20with-DJGPP%20%7C%20MinGW%20%7C%20Open%20Watcom-orange)

<img src="docs/main-menu.png" width="720" alt="The NUKED SB-EMU main menu">

</div>

---

## ✨ What it does

NUKED SB-EMU turns an ordinary PC with an onboard PCI sound chip into a full
period-correct DOS sound setup. It's tuned on a **Dell Latitude D810**
(Pentium M, Intel ICH4 AC'97, Windows 98 / MS-DOS 7.1).

| | |
|---|---|
| 🔊 **Sound Blaster 16 + OPL3** | [SBEMU](https://github.com/crazii/SBEMU) emulates SB 1.x/2.0/Pro/Pro 2/16 and OPL3 FM in pure DOS, on the PCI sound chip ([list below](#-supported-sound-chips)) |
| 🎼 **Real SC-55 firmware** | [Nuked-SC55](https://github.com/nukeykt/Nuked-SC55) runs your SC-55 / SC-55mkII ROMs behind an emulated MPU-401 |
| 🎹 **MT-32 / CM-32L** | [Munt](https://github.com/munt/munt) for games written for the Roland MT-32 |
| 🎚️ **SoundFont** | Any `.SF2` instead, with its own reverb and chorus |
| 💿 **Virtual CD drive** | `.CUE/.BIN`, `.ISO` and multi-disc images on drive **M:**, with **CD-Audio mixed into the sound** |
| 🖥️ **Menus for everything** | Start the synth, change settings, test games, play MIDI files, mount discs, with themes, fonts and display modes |
| 🎮 **USB joystick** | A wired **Xbox 360 pad** answers on the gameport (port 201h), on any USB controller *(experimental)* |
| 🪟 **Windows 98 too** | The same SB / OPL3 / MPU-401 in DOS boxes, plus an **"NUKED SB-EMU" Windows MIDI device** |
| 🐞 **Built-in diagnostics** | Start-up log, live load meter, timing tests and a one-key bug report |

> [!IMPORTANT]
> **ROMs are not included.** Bring your own SC-55 (and MT-32 / CM-32L) dumps.
> They are recognised by their contents, so the file names don't matter.

---

## 🧩 All features

<details open>
<summary><b>🔊 Sound Blaster, FM and PC speaker</b></summary>

- Sound Blaster 1.x, 2.0, Pro, Pro 2 and 16: 8-bit and 16-bit DMA, mono, stereo and high-speed modes, Creative ADPCM
- Any port (220 / 240), IRQ (5 / 7 / 9) and DMA; `BLASTER` is written for games automatically
- If the sound chip already uses the chosen IRQ, SBEMU takes another free one instead of refusing to load
- OPL3 FM: [Nuked-OPL3-fast](https://github.com/tgies/Nuked-OPL3-fast) (cycle-accurate) or DOSBox's fast core; hardware FM pass-through on cards that have it
- PC speaker sound mixed into the output
- Protected-mode games out of the box (HDPMI32i); real-mode games with JEMMEX + QPIEMU (included) or QEMM
</details>

<details>
<summary><b>🎼 MIDI: SC-55, MT-32, CM-32L, SoundFont</b></summary>

- **SC-55** (mk1 v1.00 – v2.00) and **SC-55mkII** (v1.01) firmware emulated with Nuked-SC55; ROM sets found by content, best one picked automatically
- **MT-32** and **CM-32L / LAPC-I** with Munt 2.8.3
- **SoundFont** (`.SF2`) with reverb and chorus, for slower CPUs
- MPU-401 UART on any port (330, 300, 310, 320, 332, 334); every tool finds it wherever it is
- Render-ahead buffer, timer give-back and automatic speed control keep music smooth while games run
- **Automatic setup** benchmarks each ROM set on your CPU and picks the settings it can run
- **Front-panel overlay** in games: <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>F12</kbd> shows the SC-55 / MT-32 display over the game
- `MIDPLAY`: MIDI file player with playlists, shuffle, a file browser and a live front panel
- `SC55WAV` / `SC55LOG`: render an SC-55 demo to WAV without SBEMU or a sound card and measure how fast your PC runs each ROM set
</details>

<details>
<summary><b>💿 Virtual CD drive and CD-Audio</b></summary>

- Mounts `.ISO`, `.CUE/.BIN` (raw or 2048-byte) and multi-disc games on drive **M:** (uCDD + SHSUCDX)
- **CD-Audio tracks played through the sound card**, mixed with the game's sound (`HYBRID.BAT`)
- CD images screen: folders, disc label and date, track list with music length, file list read without mounting, run a disc's installer, fix broken CUE files, eject
- Drivers for a **real CD-ROM drive** in DOS: XCDROM (included), VIDE-CDD, LG GSCDROM, MSCDEX
- Optional CD-ROM driver at every start (`BOOTCFG.BAT`), undone exactly by `RESET.BAT`
</details>

<details>
<summary><b>🖥️ Menus and tools</b></summary>

- **MENU**: sound, settings, tests, games, CD-ROM and files in one place; status box shows whether SBEMU is loaded and the port, IRQ, DMA and MIDI port it really uses; shows why SBEMU failed to load
- **Games**: per-game profiles (SBEMU options, CD image, arguments), start a game with its best settings
- **SETTINGS**: every option on one screen; changes apply to a running SBEMU at once
- **GAMETEST**: runs games unattended (Doom demos, Duke 3D with typed quit keys), measures music timing and finds each game's best settings. It sets Doom-engine and Apogee/Build games' own sound setup to SBEMU's port, IRQ, DMA and MIDI port by itself
- Seven styles (Groovy, Classic, Sunset, Ocean, Neon, Candy, Midnight), animated rainbow logo, Terminus or VGA fonts, text modes from 80×25 to 80×60 plus the VESA modes your card has (132×25 …), safe mode (hold <kbd>Shift</kbd>)
- `SBDIAG`, `MPUTEST`, `TIMETEST`, `JOYTEST`, `CPUMHZ`: measuring, test tune, timer checks, joystick view, CPU clock
- `SETUP`, `CDINST`, `CLEANUP`, `UNINSTAL`: install and remove without touching `CONFIG.SYS` or `AUTOEXEC.BAT`; uninstall keeps your ROMs and settings if you want
</details>

<details>
<summary><b>🎮 USB joystick</b></summary>

- Wired Xbox 360 pads (Microsoft and compatible) as an analog gameport joystick: two sticks, four buttons, Start/Back as Enter/Esc
- Every USB controller kind: UHCI, OHCI, EHCI and xHCI, through hubs, hot-plugging
- Axes, buttons, keys and dead zone adjustable; `JOYTEST` shows what games see. [Details below](#-usb-joystick-xbox-360-pad)
</details>

<details>
<summary><b>🪟 Windows 98</b></summary>

- Virtual Sound Blaster (1.5 – 16), OPL3 and MPU-401 for DOS boxes (`SC55W.VXD`)
- **"NUKED SB-EMU" MIDI device** for Windows programs, played by the same SC-55 / MT-32 / SoundFont synths
- Tray program with a settings window matching the DOS one; settings shared with DOS
- Installer keeps your existing `BLASTER` line and uninstalls cleanly
</details>

<details>
<summary><b>🐞 Logging and bug reports</b></summary>

- `SBLOG.TXT`: everything SBEMU printed at its last start
- `SBDETAIL.TXT`: every option and where it came from, DOS / Windows / CPU, every PCI sound device and its IRQ, each start-up step saved at once (so a hang shows where it stopped)
- **MENU → Files → Make a bug report** collects all of it, plus `CONFIG.SYS`, `AUTOEXEC.BAT`, memory and test results, into `BUGREP.TXT`
- `WIN98\SC55W.LOG`: Windows version, devices, and any audio drop-outs
</details>

---

## 🔈 Supported sound chips

The sound comes out of the PCI chip in your PC. These are every chip the
SBEMU in this pack has a driver for, taken from the drivers' own PCI ID
tables. ✅ = SBEMU lists it as tested and working; 🧪 = driver ported from
Linux, works for some people, less tested.

### Onboard (motherboard) audio

| Family | Chips | |
|---|---|:---:|
| **Intel AC'97** (`sc_ich`) | 82801AA/AB/BA (ICH, ICH0, ICH2), ICH3, **ICH4** *(Dell D810)*, ICH5, ICH6, ICH7, ESB, ESB2, 440MX | ✅ |
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
> Under emulators, QEMU's `AC97` (ICH) and `intel-hda` devices both work.

<details>
<summary><b>Hardware FM and MPU passthrough</b></summary>

On cards that have them, SBEMU can pass the OPL3 FM and MPU-401 straight
to the hardware instead of emulating them (`/SCFM`, `/SCMPU`, off by
default): C-Media CMI8338/8738 and the Yamaha YMF7x4. The SC-55 pack's own
MIDI synth replaces the MPU side, so you normally leave these off.
</details>

---

## 🎮 USB joystick (Xbox 360 pad)

<img src="docs/joytest.png" width="560" alt="JOYTEST showing the pad's sticks and buttons">

DOS games read an analog joystick on the gameport, port 201h. With `/JOY:1`
(MENU > Settings > **Joystick**), SBEMU answers there itself with a USB
Xbox 360 pad: left stick = joystick A (the D-pad moves it too), right stick
= joystick B, A/B/X/Y = buttons 1-4. Start and Back are typed as Enter and
Esc so game menus work from the pad. Sticks, buttons, keys and the dead zone
can be changed (`/JOYAX`, `/JOYBT`, `/JOYKEY`, `/JOYDZ`).

- **Pads:** the wired Microsoft pad and wired pads that speak the same
  protocol (Hori, Mad Catz, Logitech, Saitek, ...). The PC wireless receiver
  may work (first pad, untested). Not Xbox One / Series pads. Hubs are fine
  (up to four deep).
- **USB controllers: all PC kinds.** UHCI (Intel ICH / VIA, e.g. the D810),
  OHCI (NEC µPD720100/720101 PCI cards, SiS, ALi, nVidia, AMD), EHCI (USB 2.0:
  with companions every port is handed to them; without, as on Intel
  5-series and later, it is driven directly and the pad reached through the
  built-in hub's transaction translator) and xHCI (USB 3: Intel 7-series and
  later, NEC/Renesas µPD720200/201/202, ASMedia, VIA, Fresco, AMD; only its
  USB 2 ports are used, and Intel's shared ports are switched over from
  EHCI). Not CardBus cards (the BIOS leaves the slot off in DOS), not
  Renesas µPD720201/202 cards without their firmware ROM.
- **How:** a small polled USB host inside SBEMU (`sbemu/usbpad.c`): no USB
  interrupt is used, so it never fights the sound chip for an IRQ. Reports
  are read from the sound interrupt and on every port 201h read; the port's
  one-shot timers are emulated with the CPU's time stamp counter
  (`sbemu/vjoy.c`). Hot-plugging works.
- While it runs, the BIOS's own USB keyboard/mouse support stops until the
  next restart.

Tested in QEMU (UHCI; OHCI; EHCI with UHCI or OHCI companions, as on the D810
and NEC cards; EHCI alone; QEMU's xHCI and its NEC µPD720200 model; hubs on
UHCI, OHCI and xHCI; hot-plugging, also into a hub) with a pretend pad
(`sc55-pack/testrig/fakepad.py`, usbredir); not yet on a real D810.

---

## 📸 Screenshots

<table>
<tr>
<td align="center"><img src="docs/cd-images-groovy.png" width="400"><br><sub>CD images, <i>Groovy</i> style: label, tracks, CD music, installer</sub></td>
<td align="center"><img src="docs/cd-images-classic.png" width="400"><br><sub>The same screen, <i>Classic</i> style</sub></td>
</tr>
<tr>
<td align="center"><img src="docs/cd-files.png" width="400"><br><sub>Files on a disc image, read without mounting it</sub></td>
<td align="center"><img src="docs/win98-settings.png" width="400"><br><sub>Windows 98: settings, shared with DOS</sub></td>
</tr>
</table>

---

## 🧰 What's in the pack

| Program | What it does |
|---|---|
| `SBEMU.EXE` | SBEMU with the SC-55 / MT-32 / SoundFont synth built in, CD-Audio mixing, diagnostics, in-game LCD overlay (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>F12</kbd>) |
| `MENU.BAT` → `SC55MENU.EXE` | One menu for everything: sound, games (per-game profiles and CD images), CD-ROM, settings, tools |
| `SETTINGS.BAT` → `SBSET.EXE` | Every option on one screen, plus **automatic setup** that benchmarks the ROM sets and picks the best one your CPU can run |
| `GAMETEST.EXE` | Runs your games unattended and finds the settings that give each one the best music timing |
| `MIDPLAY.EXE` | MIDI player with a live SC-55 / MT-32 front panel |
| `CDSET\GUI.BAT` → `CDGUI.EXE` | CD images: mount on M:, run a disc's installer, list its files, fix CUE files |
| `JOYTEST.EXE` | Shows what games see on the gameport: four axes and four buttons, live |
| `USBTEST.EXE` | Finds the USB pad with the same code as SBEMU and shows every step (controllers, ports, plug / unplug, live input); writes a log for bug reports |
| `SBDIAG`, `MPUTEST`, `TIMETEST`, `SC55WAV` | Diagnostics, test tune, timer checks, ROM check and speed benchmark |
| `WIN98\SC55W.EXE` + `.VXD` + `SC55MIDI.DRV` | The Windows 98 part (tray program, virtual SB for DOS boxes, MIDI device) |

The release zip also carries the third-party DOS programs it may share
(HDPMI32i, JEMM / QPIEMU, uCDD, SHSUCDX, XCDROM). See [Downloads](#-downloads).

---

## ⬇️ Downloads

### NUKED SB-EMU

| | |
|---|---|
| 📦 **Latest release** | [**Download the latest NUKED-SB-EMU zip**](https://github.com/deon81997/NUKED-SBEMU-MUXED/releases/latest): DOS pack + Windows 98 part, ready to install |
| 🧾 **Source** | this repository (GPL-2.0-or-later) |

### What you have to supply yourself

The release leaves out what it may not legally share. Only the ROMs are needed for the SC-55.

| What | Needed for | Where to get it | Put it in |
|---|---|---|---|
| 🎹 **Roland SC-55 / SC-55mkII ROMs** | the SC-55 synth | Copyrighted; **dump them from a unit you own**. Not needed if you use a SoundFont | `FILES\ROMS` |
| 🎹 **Roland MT-32 / CM-32L ROMs** | the MT-32 synth (optional) | Dump them from a unit you own | `FILES\ROMS` |
| 🎚️ **A SoundFont** (`.SF2`) | the SoundFont synth (optional) | Any General MIDI SoundFont; many free ones exist | `C:\SC55\SF2` |
| 💿 **MSCDEX.EXE** (Microsoft) | your real CD-ROM drive in DOS (optional) | Part of Windows 98: SETUP takes `C:\WINDOWS\COMMAND\MSCDEX.EXE` by itself | automatic |
| 💿 **VIDE-CDD.SYS** (Acer / BenQ) | an alternative CD-ROM driver (optional; XCDROM is included) | Acer's `APICD214.EXE` package: [Vogons wiki](https://www.vogonswiki.com/index.php/VIDE-CDD.SYS), [DOS Days](https://www.dosdays.co.uk/topics/Software/optical_downloads.php) | `FILES\VIDECD` |
| 💿 **GSCDROM.SYS** (LG) | LG drives that don't work with XCDROM (optional) | [DOS Days](https://www.dosdays.co.uk/topics/Software/optical_downloads.php) | `FILES\GSCDROM` |
| 🧠 **QEMM 8 / 9** | `SC55QMM.BAT` only (optional) | Commercial, out of print. Not needed: JEMMEX + QPIEMU (included) does the same | `FILES\QEMM` |

`DOWNLOADS.TXT` in the zip has the same list.

### Third-party programs included in the release

| Program | Project | Licence |
|---|---|---|
| HDPMI32I | [HX DOS extender](https://github.com/Baron-von-Riedesel/HX), Japheth | freeware, source available |
| JEMMEX, JEMM386, JLOAD, QPIEMU | [Jemm](https://github.com/Baron-von-Riedesel/Jemm), Japheth | freeware, source available |
| UCDD 0.9.5-a2 | [uCDD](https://github.com/vorvek/uCDD), Vorvek | GPL-3.0 |
| SHSUCDX | [SHSUCD suite](https://github.com/adoxa/shsucd), Jason Hood | freeware, source available |
| XCDROM | XCDROM | GPL, source in the zip |

---

## 📋 Requirements

| | Minimum | Recommended |
|---|---|---|
| **CPU** | SoundFont / FM: a far smaller CPU than the SC-55 needs | SC-55: a fast Pentium-class CPU (the D810's Pentium M works). It runs the real firmware: `SC55WAV` or *Settings > Automatic setup* measures yours |
| **Memory** | 32 MB (about 21 MB stay resident with the SC-55) | 64 MB or more |
| **Sound** | a PCI / onboard chip from the [list above](#-supported-sound-chips) | Intel AC'97 or HD Audio |
| **DOS** | MS-DOS 6.22 / 7.x or FreeDOS, booted to plain DOS | Windows 98 "Restart in MS-DOS mode" (MS-DOS 7.1) |
| **Windows** (optional) | Windows 98 / 98 SE with a working sound driver | — |
| **Disk** | about 25 MB (more for disc images) | — |
| **Real-mode games** (optional) | JEMMEX + QPIEMU (included) or QEMM | — |

**Building from source:** [DJGPP](https://github.com/andrewwutw/build-djgpp) cross compiler (GCC 12) for DOS; for the Windows 98 part MinGW-w64 i686, [Open Watcom 2](https://github.com/open-watcom/open-watcom-v2) and [JWasm](https://github.com/Baron-von-Riedesel/JWasm).

---

## 🚀 Getting started

```text
1. Download the release zip and unzip it; put your ROM dumps in NUKEDSB\FILES\ROMS
2. Boot DOS (Windows 98: "Restart in MS-DOS mode")
3. cd to the NUKEDSB folder, run  SETUP        → installs to C:\SC55
4. Optional:                       CDINST       → virtual CD drive
5. Start:                          C:\SC55\MENU.BAT
```

In each game's setup pick **Sound Blaster 16** and **General MIDI / Sound Canvas**
(or **MT-32** if you chose it) with the values MENU shows top right: 220, IRQ 7,
DMA 1/5 and port 330 unless you changed them. GAMETEST sets Doom- and Duke-style
games itself.

<details>
<summary><b>More: real-mode games, CD music, settings</b></summary>

- **Protected-mode games** (DOS/4GW and friends): `C:\SC55\SC55.BAT`, then the game.
- **Real-mode games**: `SC55JEM.BAT` (JEMM + QPIEMU) or `SC55QMM.BAT` (QEMM); you supply those files.
- **CD music**: put the image in `C:\CDIMAGES`, mount it in the CD images screen, start with `HYBRID.BAT`.
- **Settings** live in `C:\SC55\SBOPTS.TXT`, shared by every launcher and by Windows 98.

The pack's `README.TXT` is the complete guide.
</details>

<details>
<summary><b>Windows 98</b></summary>

`SETUP98.BAT` installs to `C:\SC55\WIN98`:

- **`SC55W.VXD`**: virtual Sound Blaster (1.5 – 16), OPL3 and MPU-401 for DOS boxes
- **`SC55W.EXE`**: plays it all with the same synths; tray icon, settings window matching `SBSET`, installer / uninstaller
- **`SC55MIDI.DRV`**: the Windows MIDI device **"NUKED SB-EMU"**

See [`README98.TXT`](sc55-pack/pack/README98.TXT).
</details>

---

## 🗂️ Repository layout

```text
sc55-pack/
├── main.c, sbemu/, mpxplay/, drivers/   SBEMU + sound-chip drivers, with the pack's additions
│   └── sbemu/vmpu_sc55.c, vmpu_mt32.cpp, vmpu_sf.c, cdlink.c, vdiag.c
├── sc55/                 SC-55 engine around Nuked-SC55
├── upstream/Nuked-SC55   Nuked-SC55, unmodified
├── third_party/          Munt mt32emu 2.8.3, Nuked OPL3
├── tools/                DOS programs (menu, settings, GAMETEST, MIDPLAY, CD images …) + text UI
├── win98/                Windows 98 part: vxd/, helper/, mididrv/, compat/
├── pack/                 the pack's batch files and guides
├── testrig/              QEMU test machine (disk builder, VM driver)
└── tests/                host-side engine tests
```

📖 [`HANDOFF.md`](sc55-pack/HANDOFF.md): current state and what still needs testing &nbsp;•&nbsp;
[`README-VSC55.md`](sc55-pack/README-VSC55.md): the SC-55 options &nbsp;•&nbsp;
[`README-SBEMU.md`](sc55-pack/README-SBEMU.md): SBEMU's own guide

---

## 🔨 Building

```sh
export PATH=/path/to/djgpp/bin:$PATH   # i586-pc-msdosdjgpp-gcc (GCC 12)
cd sc55-pack
make -j2                               # → output/
```

Delete `output/*.o` after changing a header. `make VSC55=0` builds plain SBEMU.
The Windows 98 part (`win98/build98.sh`) needs MinGW-w64 i686, Open Watcom 2 and
JWasm; see [`win98/SOURCES.TXT`](sc55-pack/win98/SOURCES.TXT).

## 🐞 Reporting a bug

In DOS, open **MENU → Files → Make a bug report** (or run `BUGREP` in `C:\SC55`). It writes
`C:\SC55\BUGREP.TXT`: CONFIG.SYS, AUTOEXEC.BAT, the environment, `MEM /C`, your settings,
SBEMU's messages, SBEMU's start-up details, test results and the Windows 98 log. Attach that
file to an [issue](https://github.com/deon81997/NUKED-SBEMU-MUXED/issues).

- `SBDETAIL.TXT` is written every time SBEMU starts. It holds every option and where it came
  from, the DOS and Windows versions, the CPU, each PCI sound device with its IRQ, the card SBEMU
  chose, and each start-up step. Each line is saved at once, so after a hang the last `[step]`
  line shows where it stopped.
- `WIN98\SC55W.LOG` records the Windows version, BLASTER, memory, the wave and MIDI devices,
  and, every 30 seconds, any audio underruns or late MIDI events.
- When SBEMU fails to load, the DOS menu shows the error under the logo.

---

## 🧪 Testing

[`testrig/`](sc55-pack/testrig) boots a DOS PC in QEMU with AC'97 like the D810 and
drives it by script (keys, screenshots); `fakepad.py` plugs a pretend Xbox 360 pad
into QEMU's USB (see `testrig/README.md`). Emulators aren't the real thing, though:
the D810 has the final say.

---

## 🙏 Credits

NUKED SB-EMU stands on the work of many people. Thank you all.
Combined work: **GPL-2.0-or-later** ([`COPYING`](sc55-pack/COPYING)).

### Built into SBEMU.EXE

| Part | Authors | Licence |
|---|---|---|
| [SBEMU](https://github.com/crazii/SBEMU) | **crazii** and SBEMU's contributors | GPL-2.0+ |
| Sound-chip drivers from [MPXPlay](https://mpxplay.sourceforge.net/) | **Attila Padar** (PDSoft) | open source, used with the author's permission ([note](sc55-pack/mpxplay/au_cards/LICENSE)) |
| Additional drivers ported from Linux ALSA | **[jiyunomegami](https://github.com/jiyunomegami)**, after the Linux ALSA authors | GPL |
| [Nuked-SC55](https://github.com/nukeykt/Nuked-SC55) | **nukeykt (Nuke.YKT)**, SC-55 emulator | GPL-2.0+ |
| Nuked-SC55 [backend fork](https://github.com/jcmoyer/Nuked-SC55) used here | **J.C. Moyer (jcmoyer)**: library backend, fast H8/532 VM, ROM detection by hash | GPL-2.0+ |
| Nuked-SC55 research and help | John McMaster (PCM chip decap), org / ogamespec (deroute tool), Wohlstand, mattw, HardWareMan, giulioz, Cloudschatze, NikitaLita, Karmeck, Falcosoft, Michael Mouton | — |
| [Munt](https://github.com/munt/munt) libmt32emu 2.8.3 | **Dean Beeler, Jerome Fisher, Sergey V. Mikayev** and the Munt team | LGPL-2.1+ |
| [Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3) | **nukeykt** | LGPL-2.1+ |
| [Nuked-OPL3-fast](https://github.com/tgies/Nuked-OPL3-fast) | **Tony Gies (tgies)**: bit-exact, faster fork | LGPL-2.1+ |
| DOSBox OPL (DBOPL) and PC speaker | **The DOSBox Team**; PC speaker port to SBEMU by [andersrodrig](https://github.com/andersrodrig) | GPL-2.0+ |
| [TinySoundFont](https://github.com/schellingb/TinySoundFont) | **Bernhard Schelling**, based on SFZero by Steve Folta | MIT |
| SoundFont MPU-401 emulation in SBEMU | **[Cacodemon345](https://github.com/Cacodemon345/VSBHDASF)** (VSBHDASF) | — |
| USB host for the pad | written for this pack; UHCI / OHCI after [USBDDOS](https://github.com/crazii/USBDDOS) (**crazii**), xHCI bring-up after [xhci98](https://github.com/yeokm1/xhci98)'s DOS tool (**Yeo Kheng Meng**), pad layout after Linux `xpad.c` | GPL-2.0 |
| Terminus font | **Dimitar Zhekov** | OFL-1.1 |

### Shipped in the release zip

| Program | Authors | Licence |
|---|---|---|
| HDPMI32i | **Japheth** ([HX](https://github.com/Baron-von-Riedesel/HX)), IOPL0 build by **crazii** ([crazii/HX](https://github.com/crazii/HX)) | freeware, source available |
| JEMMEX, JEMM386, JLOAD, QPIEMU | **Japheth** ([Jemm](https://github.com/Baron-von-Riedesel/Jemm)) | see `JEMMLIC.TXT` |
| μCDD | **[vorvek](https://github.com/vorvek/uCDD)** | GPL-3.0 |
| SHSUCDX | **Jason Hood** ([shsucd](https://github.com/adoxa/shsucd)), from v1.4b by **John McCoy** | freeware, source available |
| XCDROM | **Jack R. Ellis** | GPL, source in the zip |

Not shipped, belonging to their owners: MSCDEX (Microsoft), VIDE-CDD (Acer / BenQ), GSCDROM (LG), QEMM (Quarterdeck).

### Tools used to build and test it

[DJGPP](https://www.delorie.com/djgpp/) (DJ Delorie) via [build-djgpp](https://github.com/andrewwutw/build-djgpp) (andrewwutw),
[MinGW-w64](https://www.mingw-w64.org/), [Open Watcom 2](https://github.com/open-watcom/open-watcom-v2),
[JWasm](https://github.com/Baron-von-Riedesel/JWasm) (Japheth), [QEMU](https://www.qemu.org/) and usbredir.

<sub>Roland, SC-55, MT-32 and CM-32L are trademarks of Roland Corporation. Sound Blaster is a trademark of Creative Technology. Xbox is a trademark of Microsoft. NUKED SB-EMU is not affiliated with any of them.</sub>
