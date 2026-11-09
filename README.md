<div align="center">

<table>
<tr>
<td align="center" valign="top"><img src="docs/90s/fd-marine-shoot.gif" alt="Freedoom marine"><br><br><img src="docs/90s/tux-waddle.gif" width="120" alt="Tux"><br><br><img src="docs/90s/fd-soulsphere.gif" alt="soul sphere"></td>
<td align="center">

<img src="docs/90s/title.gif" width="560" alt="NUKED SB-EMU">

<h2>~*~ Welcome to my DOS Sound Page!!! ~*~</h2>

<i>A Roland Sound Canvas for your DOS games. No MIDI hardware needed!!</i>

<br><br>

<img src="docs/90s/construction.gif" width="300" alt="Under construction">

<br>

<sub>this page is ALWAYS under construction... please come back soon!! :-)</sub>

<br><br>

<a href="https://github.com/deon81997/NUKED-SBEMU-MUXED/releases/latest"><img src="docs/90s/btn-download.gif" alt="Download"></a>
<img src="docs/90s/btn-sc55.gif" alt="SC-55 inside">
<img src="docs/90s/btn-sb16.gif" alt="Sound by SB16">
<img src="docs/90s/btn-dos.gif" alt="Powered by DOS 7.1">

<br><br>

</td>
<td align="center" valign="top"><img src="docs/90s/fd-serpentipede-attack.gif" alt="Freedoom serpentipede"><br><br><img src="docs/90s/mascot-canvaskid.gif" width="120" alt="Canvas Kid"><br><br><img src="docs/90s/fd-keycard.gif" alt="keycard"></td>
</tr>
</table>

<b>Release counter:</b><br>
<img src="docs/90s/counter.gif" alt="000072"><br>
<sub>betas and counting!</sub>

<br><br>

<img src="docs/90s/bar.gif" width="600" alt="">

<br>

<b>[ <a href="#-whats-new-">What's New</a> |
<a href="#-what-is-this-">What Is This?</a> |
<a href="#-cool-features-">Features</a> |
<a href="#-will-it-run-on-my-pc-">Will It Run?</a> |
<a href="#-getting-started-">Get Started</a> |
<a href="#-download-zone-">Downloads</a> |
<a href="#-sign-my-guestbook-">Guestbook</a> ]</b>

<img src="docs/90s/parade.gif" width="100%" alt="A parade of Freedoom monsters, the marine and Flopsy">

<img src="docs/90s/bar.gif" width="600" alt="">

</div>

<br>

<img src="docs/main-menu.png" width="720" alt="The NUKED SB-EMU main menu">

<div align="center"><sub><i>^^^ the main menu, running in plain DOS ^^^</i></sub></div>

<div align="center"><img src="docs/90s/fd-marine-walk.gif" alt=""> <img src="docs/90s/ic-news.gif" alt=""> <img src="docs/90s/bar.gif" width="380" alt=""> <img src="docs/90s/ic-news.gif" alt=""> <img src="docs/90s/fd-serpentipede.gif" alt=""></div>

## ★ What's NEW!! ★

<img src="docs/90s/new.gif" alt="NEW!"> **11/09/2026: beta.75.** **Joysticks in DOS boxes!!** Any controller Windows 98
knows about now works in DOS games inside Windows too, on the game port and through the BIOS, so a 360 pad with its
Windows driver can play your DOS games without leaving Windows. And Windows games' music volume sliders now work
on the NUKED SB-EMU MIDI device.

<img src="docs/90s/new.gif" alt="NEW!"> **11/09/2026: beta.74.** The **Windows 98 part** gets its first update in ages!!
It now has the faster SC-55 too, and it can't hog the CPU any more: when the synth can't keep up, the music lags
instead of Windows. Started with Windows, it waits 15 seconds so Windows can finish starting first. And if Windows
ever freezes while it's starting, the next boot skips it and tells you where it got stuck.

<img src="docs/90s/new.gif" alt="NEW!"> **11/09/2026: beta.73.** SBEMU loads more than **twice as fast**!! On
a 366 MHz Celeron it went from 15 seconds to under 7. A ROM check was being redone almost every time by
mistake, and now it isn't. Plus a garbled line on the start screen is fixed.

<img src="docs/90s/new.gif" alt="NEW!"> **11/09/2026: beta.72.** MIDPLAY goes **3D**!! Press S until you get a
little sound module with the synth's live display, a keyboard that lights up as the notes play, and dancing
channel columns. And <kbd>F3</kbd> now switches the colour style in *every* program, not just the menu.

<img src="docs/90s/new.gif" alt="NEW!"> **11/09/2026: beta.71.** CUE images with CD music can be taken out and
swapped again! Before, they got stuck on drive M: until you rebooted. Oops.

<img src="docs/90s/new.gif" alt="NEW!"> **11/09/2026: beta.70.** Big bug hunt! MIDPLAY redraws properly after
the file browser, all the menu text fits now, and Automatic setup is minutes
faster on slow PCs.

<img src="docs/90s/new.gif" alt="NEW!"> **11/09/2026: beta.69.** The SC-55 got **~20% faster**!! It sounds
*exactly* the same (I checked every single sample), it just does less
pointless work.

**11/08/2026: beta.68.** Dark Forces' setup doesn't freeze with the SC-55
anymore.

<details>
<summary><b>&gt;&gt; Click here to read how the speed-up works &lt;&lt;</b></summary>

<br>

The SC-55 is a little computer inside a box: a Hitachi H8 CPU running Roland's
firmware, plus a custom sound chip. Nuked-SC55 emulates all of it, every clock
tick. That's why it sounds right, and that's why it's hungry. beta.69 cuts
out work that never changes the result:

* the chip's timers only get stepped when a tick actually matters
* wave ROM reads use a lookup table instead of a chain of checks
* the CPU loop skips the sound chip and the A/D converter while they're idle

Output was compared against the old version sample by sample, on every ROM
set, with effects on and off: identical. It's about 19% fewer instructions per
second of music, which came out as **+24% on a Celeron 366** (0.28× → 0.35×
real time).

And since beta.68, with automatic speed control on (the default), the synth
never takes more than about 60% of the CPU, so your game always gets the rest.
</details>

<div align="center"><img src="docs/90s/fd-zombie.gif" alt=""> <img src="docs/90s/ic-sparkle.gif" alt=""> <img src="docs/90s/bar.gif" width="380" alt=""> <img src="docs/90s/ic-sparkle.gif" alt=""> <img src="docs/90s/mascot-flopsy.gif" alt=""></div>

## ★ What is this?? ★

Ever fire up Doom on an old laptop and the music starts and it's... FM? It's
fine, but you *know* how it's supposed to sound. It's supposed to sound like a
**Roland Sound Canvas**.

NUKED SB-EMU puts one inside your PC! It's [SBEMU](https://github.com/crazii/SBEMU)
(a Sound Blaster 16 + OPL3 emulator that runs in plain DOS on PCI sound chips)
with a real SC-55 built in: [Nuked-SC55](https://github.com/nukeykt/Nuked-SC55)
runs the actual Roland firmware from your own ROM dumps. So it's not "a
SoundFont that sounds kinda close". It's the real thing!! Then I added a
virtual CD drive with CD music, USB gamepads in DOS, a Windows 98 side, and
menus for all of it.

It's made and tested on my **Dell Latitude D810** (Pentium M, ICH6, Win98SE /
DOS 7.1), plus a lot of testing in QEMU and 86Box. It's still beta, so if
something breaks please [sign my guestbook](#-sign-my-guestbook-) and tell me!

> [!IMPORTANT]
> **ROMs are NOT included!!** Bring your own SC-55 (and MT-32 / CM-32L) dumps.
> They're recognised by what's inside them, so the file names don't matter.

<div align="center"><img src="docs/90s/fd-hatchling.gif" alt=""> <img src="docs/90s/ic-speaker.gif" alt=""> <img src="docs/90s/bar.gif" width="380" alt=""> <img src="docs/90s/ic-speaker.gif" alt=""> <img src="docs/90s/fd-soulsphere.gif" alt=""></div>

## ★ Cool Features ★

<table>
<tr><td>🔊</td><td><b>Sound Blaster 1.x / 2.0 / Pro / 16 + OPL3 FM</b> in pure DOS, on your PCI sound chip</td></tr>
<tr><td>🎹</td><td><b>Roland SC-55 and SC-55mkII</b>: the real firmware, from your ROMs</td></tr>
<tr><td>🎼</td><td><b>Roland MT-32 / CM-32L</b> (Munt), or any <code>.SF2</code> SoundFont</td></tr>
<tr><td>💿</td><td><b>Virtual CD drive M:</b>: ISO, CUE/BIN, multi-disc, with CD music mixed in</td></tr>
<tr><td>🎮</td><td><b>USB Xbox 360 pads</b> as gameport joysticks (up to two), plus USB keyboard and mouse</td></tr>
<tr><td>🪟</td><td><b>Windows 98</b>: SB / OPL3 / MPU for DOS boxes, and a "NUKED SB-EMU" MIDI device</td></tr>
<tr><td>🖥️</td><td><b>Menus for everything</b>: seven colour styles, a 3D menu, game profiles, a MIDI player, tests</td></tr>
<tr><td>🐞</td><td><b>One-key bug report</b>, start-up logs and a live load meter</td></tr>
</table>

<details>
<summary><b>&gt;&gt; Sound Blaster, FM and PC speaker &lt;&lt;</b></summary>

- Sound Blaster 1.x, 2.0, Pro, Pro 2 and 16: 8- and 16-bit DMA, mono, stereo, high-speed modes, Creative ADPCM
- Any port (220 / 240), IRQ (5 / 7 / 9) and DMA; `BLASTER` is set for games automatically
- If your sound chip already sits on the IRQ you picked, SBEMU grabs a free one instead of giving up
- OPL3 FM: [Nuked-OPL3-fast](https://github.com/tgies/Nuked-OPL3-fast) (cycle-accurate) or DOSBox's fast core; hardware FM pass-through on cards that have it
- PC speaker sound mixed into the output
- Protected-mode games out of the box (HDPMI32i); real-mode games with JEMMEX + QPIEMU (included) or QEMM
- 16-bit protected-mode games (Borland Pascal / C++ DPMI, like Jazz Jackrabbit) with SBEMU16 + HDPMI16I (`SC55J16.BAT`)
</details>

<details>
<summary><b>&gt;&gt; MIDI: SC-55, MT-32, CM-32L, SoundFont &lt;&lt;</b></summary>

- **SC-55** (mk1 v1.00 – v2.00) and **SC-55mkII** (v1.01), ROM sets found by content, the best one picked for you
- **MT-32** and **CM-32L / LAPC-I** with Munt 2.8.5
- **SoundFont** (`.SF2`) with reverb and chorus, for slower PCs
- MPU-401 on any port (330, 300, 310, 320, 332, 334)
- Render-ahead buffer and automatic speed control keep the music smooth while games run
- **Automatic setup** measures each ROM set on your CPU and picks the best one it can handle
- **Front panel in games**: <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>F12</kbd> shows the SC-55 / MT-32 display over the game
- `MIDPLAY`: MIDI player with playlists, shuffle, a file browser, a live front panel and a 3D view
- `SC55WAV` / `SC55LOG`: render an SC-55 demo to WAV and see how fast your PC runs each ROM set
</details>

<details>
<summary><b>&gt;&gt; Virtual CD drive and CD music &lt;&lt;</b></summary>

- Mounts `.ISO`, `.CUE/.BIN` (raw or 2048-byte) and multi-disc games on drive **M:** (uCDD + SHSUCDX)
- **CD music played through your sound card**, mixed with the game (`HYBRID.BAT`)
- Swap discs while SBEMU runs; the music stops when a disc comes out
- CD images screen: disc label and date, track list with lengths, file list without mounting, run a disc's installer, fix broken CUE files
- Drivers for a **real CD-ROM drive** in DOS: XCDROM (included), VIDE-CDD, LG GSCDROM, MSCDEX
</details>

<details>
<summary><b>&gt;&gt; Menus and tools &lt;&lt;</b></summary>

- **MENU**: sound, settings, tests, games, CD-ROM and files in one place; shows whether SBEMU is loaded and the port, IRQ, DMA and MIDI port it really uses
- **Games**: per-game profiles (SBEMU options, CD image, arguments)
- **SETTINGS**: every option on one screen; changes apply to a running SBEMU straight away
- <kbd>F3</kbd> in any program switches the colour style for all of them
- **GAMETEST**: runs games by itself (Doom demos, Duke 3D), measures music timing and finds each game's best settings
- Styles: Groovy, Classic, Sunset, Ocean, Neon, Candy, Midnight; a 3D menu (640×480, no 3D card needed); Terminus or VGA fonts; 80×25 up to 80×60 and your card's VESA text modes
- `SBDIAG`, `MPUTEST`, `TIMETEST`, `JOYTEST`, `USBTEST`, `KMTEST`, `CPUMHZ`
- `SETUP`, `CDINST`, `CLEANUP`, `UNINSTAL`: install and remove without touching `CONFIG.SYS` or `AUTOEXEC.BAT`
</details>

<details>
<summary><b>&gt;&gt; Windows 98 &lt;&lt;</b></summary>

- Virtual Sound Blaster (1.5 – 16), OPL3 and MPU-401 for DOS boxes (`SC55W.VXD`)
- **"NUKED SB-EMU" MIDI device** for Windows programs, played by the same synths
- Tray program with a settings window matching the DOS one; settings shared with DOS
- See [`README98.TXT`](sc55-pack/pack/README98.TXT)
</details>

<div align="center"><img src="docs/90s/fd-worm.gif" alt=""> <img src="docs/90s/ic-chip.gif" alt=""> <img src="docs/90s/bar.gif" width="380" alt=""> <img src="docs/90s/ic-chip.gif" alt=""> <img src="docs/90s/fd-trilobite.gif" alt=""></div>

## ★ Will it run on my PC?? ★

Sound Blaster and FM run on anything with a Pentium. The SC-55 is the hungry
one, because it emulates Roland's hardware clock by clock. It needs a CPU with
CMOV (Pentium Pro / II or newer).

**What I measured** with `SC55LOG` / `SC55WAV` (1.0× = exactly real time):

| CPU | SC-55 mk1 v2.00 | |
|---|---|---|
| Celeron 366 (86Box) | **0.35×** (was 0.28×) | too slow, use a SoundFont |
| Celeron 533 (86Box, before beta.69) | 0.41× | too slow, but SB + FM are fine |
| Pentium M (my Dell D810) | plays live | the home machine!! |

**My best guess** for other CPUs, worked out from those numbers. Run
*Settings > Automatic setup* on yours for the real answer:

| SC-55 | Just about plays (≈1.0×) | Comfy (≈1.5×, the game keeps its share) |
|---|---|---|
| **mk1** | Pentium III / Celeron ≈ 1 GHz, Pentium M ≈ 900 MHz, Athlon XP 1200+ | **Pentium M ≈ 1.2 GHz, Pentium 4 ≈ 2.0 GHz, Athlon XP 1600+** |
| **mkII** (≈1.6–1.9× heavier) | Pentium M ≈ 1.5 GHz, Pentium 4 ≈ 2.4 GHz | Pentium M ≈ 2 GHz, Pentium 4 ≈ 3 GHz, Athlon 64 and up |

Too slow? Pick a **SoundFont** instead. It runs on a Pentium II no problem.
(Turning reverb/chorus off only helps a little: most of the work is the
emulated CPU, and that always runs at full speed.)

| You also need | Minimum | Nice to have |
|---|---|---|
| **Memory** | 32 MB (about 21 MB stay resident with the SC-55) | 64 MB or more |
| **Sound** | a PCI / onboard chip from the [big list](#-supported-sound-chips-) | Intel AC'97 or HD Audio |
| **DOS** | MS-DOS 6.22 / 7.x or FreeDOS, plain DOS | Windows 98 "Restart in MS-DOS mode" |
| **Disk** | about 25 MB, more for disc images | |

<div align="center"><img src="docs/90s/fd-potion.gif" alt=""> <img src="docs/90s/ic-rocket.gif" alt=""> <img src="docs/90s/bar.gif" width="380" alt=""> <img src="docs/90s/ic-rocket.gif" alt=""> <img src="docs/90s/mascot-flopsy.gif" alt=""></div>

## ★ Getting Started ★

```text
  1. Download the zip, unzip it, put your ROM dumps in NUKEDSB\FILES\ROMS
  2. Boot DOS          (Windows 98: "Restart in MS-DOS mode")
  3. cd NUKEDSB
     SETUP             installs to C:\SC55
     CDINST            optional: virtual CD drive
  4. C:\SC55\MENU      and have fun!!
```

In each game's setup pick **Sound Blaster 16** and **General MIDI / Sound
Canvas** (or **MT-32**) with the values the menu shows in the top right corner:
usually 220, IRQ 7, DMA 1/5 and port 330. GAMETEST sets up Doom- and
Duke-style games by itself.

<details>
<summary><b>&gt;&gt; More: real-mode games, CD music, settings &lt;&lt;</b></summary>

- **Protected-mode games** (DOS/4GW and friends): `C:\SC55\SC55.BAT`, then the game
- **Real-mode games**: `SC55JEM.BAT` (JEMM + QPIEMU) or `SC55QMM.BAT` (QEMM, bring your own)
- **CD music**: put the image in `C:\CDIMAGES`, mount it in the CD images screen, start with `HYBRID.BAT`
- **Settings** live in `C:\SC55\SBOPTS.TXT`, shared by every launcher and Windows 98

The `README.TXT` in the pack is the full manual.
</details>

<div align="center"><img src="docs/90s/fd-marine-walk.gif" alt=""> <img src="docs/90s/ic-pad.gif" alt=""> <img src="docs/90s/bar.gif" width="380" alt=""> <img src="docs/90s/ic-pad.gif" alt=""> <img src="docs/90s/fd-keycard.gif" alt=""></div>

## ★ USB Gamepads in DOS?!? ★

<img src="docs/joytest.png" width="560" alt="JOYTEST showing the pad's sticks and buttons">

Yep!! DOS games read an analog joystick on the gameport (port 201h), and SBEMU
answers there with a USB Xbox 360 pad: left stick = joystick A (the D-pad
too), right stick = joystick B, A/B/X/Y = buttons 1-4, and Start / Back type
Enter / Esc. Two pads? Pad 1 is joystick A and pad 2 is joystick B. Games that
ask the BIOS for the joystick get the pad too.

- **Stick types** (`/JOYMODE`): standard, CH Flightstick Pro, Thrustmaster FCS, Gravis GamePad Pro (GrIP), or native (a real gameport)
- **USB keyboard** typed through the PC's keyboard controller, so DOS sees a PS/2 keyboard
- **USB mouse** through a DOS mouse driver (CuteMouse is included and loaded for you)
- **Pads:** the wired Microsoft pad and wired lookalikes (Hori, Mad Catz, Logitech, Saitek...). Not Xbox One / Series pads, sorry!
- **Controllers:** UHCI, OHCI, EHCI and xHCI, through hubs (up to four deep), hot-plugging. Not CardBus cards
- **How:** a tiny polled USB host inside SBEMU (`sbemu/usbpad.c`). It uses no USB interrupt, so it never fights the sound chip for an IRQ

Works on my D810. Hubs are tested in QEMU on every controller type; a pad
behind a USB 2.0 hub on a USB 2.0 port still needs a real-hardware test.

<div align="center"><img src="docs/90s/mascot-canvaskid.gif" width="96" alt=""> <img src="docs/90s/ic-speaker.gif" alt=""> <img src="docs/90s/bar.gif" width="380" alt=""> <img src="docs/90s/ic-speaker.gif" alt=""> <img src="docs/90s/fd-serpentipede.gif" alt=""></div>

## ★ Supported Sound Chips ★

Sound comes out of the PCI chip in your PC. These are all the chips the SBEMU
in this pack has a driver for. ✅ = tested and working; 🧪 = ported from Linux,
works for some people.

<details>
<summary><b>&gt;&gt; Onboard (motherboard) audio &lt;&lt;</b></summary>

| Family | Chips | |
|---|---|:---:|
| **Intel AC'97** (`sc_ich`) | 82801AA/AB/BA (ICH, ICH0, ICH2), ICH3, ICH4, ICH5, **ICH6** *(my D810)*, ICH7, ESB, ESB2, 440MX | ✅ |
| **nVidia AC'97** (`sc_ich`) | nForce, nForce2, nForce3, MCP04, CK8, CK8S, CK804 | ✅ |
| **SiS / AMD AC'97** (`sc_ich`) | SiS 7012, AMD-768, AMD-8111 | ✅ |
| **VIA AC'97** (`sc_via82`) | VT82C686, VT8233 / 8233A / 8235 / 8237 | ✅ |
| **Intel HD Audio** (`sc_inthd`) | ICH6 – ICH10, ESB2, 5 Series / 3400, Cougar Point, Patsburg, Panther Point, Lynx Point (+LP), 9 Series, Wildcat Point, Wellsburg, Lewisburg, Sunrise Point, Kaby Lake, Coffee Lake, Cannon Lake, Comet Lake, Ice Lake, Jasper Lake, Tiger Lake, Alder Lake, Elkhart Lake, Broxton, Gemini Lake, Bay Trail, Braswell, Poulsbo, Oaktrail | ✅ |
| **AMD / ATI HD Audio** (`sc_inthd`) | ATI SB450, SB600, AMD Hudson, X370-class, X570-class, Stoney, Raven | ✅ |
| **nVidia HD Audio** (`sc_inthd`) | MCP51, MCP55, MCP61, MCP65, MCP67, MCP73, MCP77, MCP79, MCP89 | ✅ |
| **Other HD Audio** (`sc_inthd`) | VIA 82xx / VT7122 / VT6122, SiS 966, ULi M5461, Zhaoxin, C-Media HDA, Vortex86MX, Teradici | ✅ |
| **HDMI audio** (`sc_inthd`) | Intel Haswell / Broadwell HDMI, ATI/AMD Radeon HDMI (RS600 – R6xxx and later) | 🧪 |
| **Geode** (`sc_cs5535`) | National CS5535, AMD CS5536 | 🧪 |
</details>

<details>
<summary><b>&gt;&gt; Sound cards &lt;&lt;</b></summary>

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
</details>

> [!TIP]
> Not sure what's in your PC? Boot DOS and run `SBEMU` once. It tells you the chip it found.

<div align="center"><img src="docs/90s/fd-hatchling.gif" alt=""> <img src="docs/90s/ic-camera.gif" alt=""> <img src="docs/90s/bar.gif" width="380" alt=""> <img src="docs/90s/ic-camera.gif" alt=""> <img src="docs/90s/fd-zombie.gif" alt=""></div>

## ★ Screenshots!! ★

<div align="center">

<b>The menu in all seven styles</b> (Groovy, Classic, Sunset, Ocean, Neon, Candy, Midnight)<br>
<img src="docs/shots/menu-styles.gif" width="750" alt="The text menu cycling through its seven styles">

<br><br>

<b>...or in 3D!</b> 640×480 VESA, drawn by the CPU, no 3D card needed. <kbd>F3</kbd> switches the look:

<table>
<tr>
<td align="center"><img src="docs/shots/menu-3d-panels.png" width="280"><br><sub><i>Panels</i>: raised buttons</sub></td>
<td align="center"><img src="docs/shots/menu-3d-carousel.png" width="280"><br><sub><i>Carousel</i>: cards on a turning ring</sub></td>
<td align="center"><img src="docs/shots/menu-3d-module.png" width="280"><br><sub><i>Module</i>: a sound module whose LCD and knob are the menu</sub></td>
</tr>
</table>

<b>MIDPLAY</b>, the MIDI player, live off the SC-55, now with a <b>3D view</b>:

<img src="docs/shots/midplay-3d.png" width="640" alt="MIDPLAY's 3D view: a sound module, a lit keyboard and channel columns">

<table>
<tr>
<td align="center"><img src="docs/shots/midplay-panel.png" width="420"><br><sub>front panel view: the emulated SC-55 display, live</sub></td>
<td align="center"><img src="docs/shots/midplay-channels.png" width="420"><br><sub>channel view: instruments, levels and held notes</sub></td>
</tr>
</table>

<b>Settings, Automatic setup and the tests</b>:

<table>
<tr>
<td align="center"><img src="docs/shots/settings.png" width="420"><br><sub>SETTINGS: every option on one screen</sub></td>
<td align="center"><img src="docs/shots/automatic-setup.png" width="420"><br><sub>Automatic setup measures your CPU and picks the settings</sub></td>
</tr>
<tr>
<td align="center"><img src="docs/shots/timetest.png" width="420"><br><sub>TIMETEST: timer and render-speed check</sub></td>
<td align="center"><img src="docs/shots/sbdiag.png" width="420"><br><sub>SBDIAG: what SBEMU found and uses</sub></td>
</tr>
<tr>
<td align="center"><img src="docs/shots/gametest.png" width="420"><br><sub>GAMETEST: runs your games and finds their best settings</sub></td>
<td align="center"><img src="docs/main-menu.png" width="420"><br><sub>the main menu, <i>Sunset</i> style</sub></td>
</tr>
</table>

<b>The virtual CD drive</b>:

<table>
<tr>
<td align="center"><img src="docs/shots/cd-images-midnight.png" width="420"><br><sub>CD images, <i>Midnight</i> style: tracks, CD music, fixes</sub></td>
<td align="center"><img src="docs/cd-images-groovy.png" width="420"><br><sub>CD images, <i>Groovy</i> style</sub></td>
</tr>
<tr>
<td align="center"><img src="docs/cd-files.png" width="420"><br><sub>files on a disc image, read without mounting it</sub></td>
<td align="center"><img src="docs/cd-images-classic.png" width="420"><br><sub>CD images, <i>Classic</i> style</sub></td>
</tr>
</table>

<b>USB pads and Windows 98</b>:

<table>
<tr>
<td align="center"><img src="docs/joytest.png" width="420"><br><sub>JOYTEST: what games see from your USB pad</sub></td>
<td align="center"><img src="docs/win98-settings.png" width="420"><br><sub>Windows 98 settings, shared with DOS</sub></td>
</tr>
</table>

</div>

<div align="center"><img src="docs/90s/fd-potion.gif" alt=""> <img src="docs/90s/ic-cd.gif" alt=""> <img src="docs/90s/bar.gif" width="380" alt=""> <img src="docs/90s/ic-cd.gif" alt=""> <img src="docs/90s/fd-soulsphere.gif" alt=""></div>

## ★ Download Zone ★

<table>
<tr>
<td><img src="docs/90s/floppy.gif" alt="floppy"></td>
<td><b><a href="https://github.com/deon81997/NUKED-SBEMU-MUXED/releases/latest">&gt;&gt; CLICK HERE TO DOWNLOAD THE LATEST ZIP &lt;&lt;</a></b><br>
<sub>the DOS pack plus the Windows 98 part, ready to install. Source is right here (GPL-2.0-or-later)</sub></td>
</tr>
</table>

**Stuff you bring yourself** (the zip can't legally include these; for the SC-55 you only need the ROMs):

| What | For | Where to get it | Goes in |
|---|---|---|---|
| **Roland SC-55 / SC-55mkII ROMs** | the SC-55 | Copyrighted: **dump them from a unit you own**. Not needed with a SoundFont | `FILES\ROMS` |
| **Roland MT-32 / CM-32L ROMs** | the MT-32 (optional) | Dump them from a unit you own | `FILES\ROMS` |
| **A SoundFont** (`.SF2`) | the SoundFont synth (optional) | Any General MIDI SoundFont; lots are free | `C:\SC55\SF2` |
| **MSCDEX.EXE** (Microsoft) | a real CD-ROM drive (optional) | Comes with Windows 98: SETUP finds it by itself | automatic |
| **VIDE-CDD.SYS** (Acer / BenQ) | another CD-ROM driver (optional; XCDROM is included) | Acer's `APICD214.EXE`: [Vogons wiki](https://www.vogonswiki.com/index.php/VIDE-CDD.SYS), [DOS Days](https://www.dosdays.co.uk/topics/Software/optical_downloads.php) | `FILES\VIDECD` |
| **GSCDROM.SYS** (LG) | LG drives XCDROM doesn't like (optional) | [DOS Days](https://www.dosdays.co.uk/topics/Software/optical_downloads.php) | `FILES\GSCDROM` |
| **QEMM 8 / 9** | `SC55QMM.BAT` only (optional) | Out of print; JEMMEX + QPIEMU (included) does the same job | `FILES\QEMM` |

<div align="center"><img src="docs/90s/mascot-flopsy.gif" alt=""> <img src="docs/90s/ic-news.gif" alt=""> <img src="docs/90s/bar.gif" width="380" alt=""> <img src="docs/90s/ic-news.gif" alt=""> <img src="docs/90s/fd-worm.gif" alt=""></div>

## ★ Sign My Guestbook! ★

<table>
<tr>
<td><img src="docs/90s/mail.gif" alt="mailbox"></td>
<td>Found a bug? Got it working on your PC? <b><a href="https://github.com/deon81997/NUKED-SBEMU-MUXED/issues">Sign my guestbook (open an issue)!!</a></b></td>
</tr>
</table>

In DOS, open **MENU → Files → Make a bug report** (or run `BUGREP` in
`C:\SC55`). It writes `C:\SC55\BUGREP.TXT` with your config files, memory,
settings, SBEMU's messages and test results. Attach it and tell me what you
were doing. If SBEMU hung, the last `[step]` line in `SBDETAIL.TXT` shows
exactly where.

<div align="center"><img src="docs/90s/fd-trilobite.gif" alt=""> <img src="docs/90s/ic-computer.gif" alt=""> <img src="docs/90s/bar.gif" width="380" alt=""> <img src="docs/90s/ic-computer.gif" alt=""> <img src="docs/90s/fd-marine-shoot.gif" alt=""></div>

## ★ For the Nerds: Source Code ★

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

Build it:

```sh
export PATH=/path/to/djgpp/bin:$PATH   # i586-pc-msdosdjgpp-gcc (GCC 12)
cd sc55-pack
make -j2                               # -> output/
```

`make VSC55=0` builds plain SBEMU. The Windows 98 part (`win98/build98.sh`)
needs MinGW-w64 i686, Open Watcom 2 and JWasm; see
[`win98/SOURCES.TXT`](sc55-pack/win98/SOURCES.TXT).
[`HANDOFF.md`](sc55-pack/HANDOFF.md) is the dev diary: what changed, how it was
tested, what still needs real hardware. The 90s graphics on this page are
drawn by [`docs/90s/make90s.py`](docs/90s/make90s.py).

<div align="center"><img src="docs/90s/tux-waddle.gif" width="96" alt=""> <img src="docs/90s/ic-heart.gif" alt=""> <img src="docs/90s/bar.gif" width="380" alt=""> <img src="docs/90s/ic-heart.gif" alt=""> <img src="docs/90s/mascot-canvaskid.gif" width="96" alt=""></div>

## ★ Thanks To My Friends ★

None of this would exist without these awesome people. THANK YOU!!
The whole thing is **GPL-2.0-or-later** ([`COPYING`](sc55-pack/COPYING)).

<details open>
<summary><b>&gt;&gt; Built into SBEMU.EXE &lt;&lt;</b></summary>

| Part | Authors | Licence |
|---|---|---|
| [SBEMU](https://github.com/crazii/SBEMU) | **crazii** and SBEMU's contributors | GPL-2.0+ |
| Sound-chip drivers from [MPXPlay](https://mpxplay.sourceforge.net/) | **Attila Padar** (PDSoft) | open source, used with the author's permission ([note](sc55-pack/mpxplay/au_cards/LICENSE)) |
| More drivers ported from Linux ALSA | **[jiyunomegami](https://github.com/jiyunomegami)**, after the Linux ALSA authors | GPL |
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
</details>

<details open>
<summary><b>&gt;&gt; Shipped in the zip &lt;&lt;</b></summary>

| Program | Authors | Licence |
|---|---|---|
| HDPMI32i, HDPMI16I | **Japheth (Andreas Grech)** ([HX](https://github.com/Baron-von-Riedesel/HX)); port-trap API and IOPL0 build by **crazii** ([crazii/HX](https://github.com/crazii/HX)); HDPMI16I built from crazii/HX with a two-line patch ([notes](sc55-pack/third_party/hdpmi16/README.TXT)) | freeware, source available |
| JEMMEX, JEMM386, JLOAD, QPIEMU | **Japheth** ([Jemm](https://github.com/Baron-von-Riedesel/Jemm)) | see `JEMMLIC.TXT` |
| μCDD 0.9.5-a2 | **[vorvek](https://github.com/vorvek/uCDD)**; this copy adds a CD-audio link for SBEMU and the beta.71 unmount fix ([`scripts/ucdd-unmount-fix.py`](sc55-pack/scripts/ucdd-unmount-fix.py)) | GPL-3.0 |
| SHSUCDX | **Jason Hood** ([shsucd](https://github.com/adoxa/shsucd)), from v1.4b by **John McCoy** | freeware, source available |
| XCDROM | **Jack R. Ellis** | GPL, source in the zip |
| [CuteMouse](https://cutemouse.sourceforge.net/) 2.1b4 | **Nagy Daniel**, **Arkady V. Belousov**, 2.1 betas by **Eric Auer**, and contributors | GPL-2.0, source in the zip |
</details>

<details open>
<summary><b>&gt;&gt; The characters on this page &lt;&lt;</b></summary>

| Who | Authors | Licence |
|---|---|---|
| The marine, serpentipede, hatchling, zombie, trilobite, worm, soul sphere, potion and keycard | **Contributors to the [Freedoom](https://freedoom.github.io/) project** (sprites from [freedoom/freedoom](https://github.com/freedoom/freedoom)) | BSD-3-Clause, [`docs/90s/src/FREEDOOM-COPYING.txt`](docs/90s/src/FREEDOOM-COPYING.txt) |
| Tux the penguin | **Larry Ewing** (lewing@isc.tamu.edu), made with The GIMP | free to use with credit, [`docs/90s/src/TUX-COPYING.txt`](docs/90s/src/TUX-COPYING.txt) |
| Flopsy, the Canvas Kid, buttons, signs and icons | drawn for this page by [`make90s.py`](docs/90s/make90s.py) and [`sprites90s.py`](docs/90s/sprites90s.py) | GPL-2.0+ like the rest |

</details>

Not shipped, belonging to their owners: MSCDEX and MOUSE.COM (Microsoft),
VIDE-CDD (Acer / BenQ), GSCDROM (LG), QEMM (Quarterdeck). Built with
[DJGPP](https://www.delorie.com/djgpp/) via [build-djgpp](https://github.com/andrewwutw/build-djgpp),
[MinGW-w64](https://www.mingw-w64.org/), [Open Watcom 2](https://github.com/open-watcom/open-watcom-v2) and
[JWasm](https://github.com/Baron-von-Riedesel/JWasm); tested in [QEMU](https://www.qemu.org/) and [86Box](https://86box.net/).

<div align="center">

<img src="docs/90s/bar.gif" width="600" alt="">

<h3>~ DOS Sound WebRing ~</h3>

<b>[ <a href="https://github.com/crazii/SBEMU">SBEMU</a> |
<a href="https://github.com/nukeykt/Nuked-SC55">Nuked-SC55</a> |
<a href="https://github.com/munt/munt">Munt</a> |
<a href="https://github.com/vorvek/uCDD">uCDD</a> |
<a href="https://github.com/Baron-von-Riedesel/HX">HX</a> ]</b>

<br><br>

<img src="docs/90s/btn-ie.gif" alt="Viewed with IE 4.0">
<img src="docs/90s/btn-800.gif" alt="Best viewed at 800x600">
<img src="docs/90s/btn-notepad.gif" alt="Made with Notepad">
<img src="docs/90s/btn-gpl.gif" alt="GPL 2.0+">

<br><br>

<sub>Last updated: November 9, 2026 &nbsp;·&nbsp; made for fun on a Dell D810 &nbsp;·&nbsp; bring your own ROMs!!</sub>

<sub>Roland, SC-55, MT-32 and CM-32L are trademarks of Roland Corporation. Sound Blaster is a trademark of Creative Technology. Xbox is a trademark of Microsoft. NUKED SB-EMU is not affiliated with any of them.</sub>

</div>
