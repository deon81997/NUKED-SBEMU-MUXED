# Harvest (2026-11-13)

Collected from the user's Claude chats on 4 accounts (86 project chats, 2026-10-28 to 11-13). Lessons went to LESSONS.md.
Refresh with the "Harvest lessons" prompt in the playbook.

## Test machines

| Machine | Details |
| --- | --- |
| Dell Latitude D810 laptop | Pentium M 2 GHz (SSE2), ATI Mobility Radeon X300, ICH6, Windows 98 SE + MS-DOS 7.1. Main target. |
| ASUS P5PE-VM desktop | Intel 865G/ICH5, Sound Blaster Audigy, Leadtek GeForce 6800 (A400), 2 GB RAM, 3x DVD-ROM, Marvell Yukon 88E8001 LAN, Belkin F5U219 USB 2.0 card (NEC uPD720101GJ, no option ROM), wired Xbox 360 pad, Logitech receiver. Win98/DOS. |
| ASRock AM2NF3-VSTA | Retro PC with Yamaha YMF744/754 card; goal: DOS sound without DDMA. |
| Main desktop | Ryzen 5 5600, 32 GB RAM, Quadro RTX 5000 (dev/host machine). |

## Hardware reports worth keeping

- D810: USB mouse random moves/clicks in BIOS mode (KBC idle-before-byte; fixed beta.78, confirm on hardware).
- D810: 48 kHz output garbled, 22 kHz fine (reported in the 86Box chat); 3D player felt slow.
- D810: freezes during SC55.BAT were reported once (DDMA chat); later builds changed init.
- P5PE-VM: freeze with onboard USB + NEC card both on (fix beta.110, confirm on hardware).
- P5PE-VM + NEC card: wired 360 pad detected but no buttons in JOYTEST (check status).
- NEC card: 3D GPF with USB HID on OHCI (beta.102 fix, confirm on hardware).
- Virtual CD: "read error in DOS" when starting games after Win98 boot menu -> DOS.
- Win98 -> DOS transition problems with the pack (see SweetLow note in LESSONS.md).

## Ideas the user asked for (check HANDOFF before starting; some may be done)

- Yamaha YMF7x4 emulation / DDMA for DOS and 9x (AM2NF3).
- SB16 emulation in Windows 98 with the same options as DOS.
- Windows joystick menu built on the custom 360 driver; option to choose emulated or native joystick.
- Controller layouts database from PCGamingWiki matching games' keyboard controls.
- MIDPLAY: vector and voxel 3D modes, more effects/themes, low-pass filter, use video card memory, SC-55 screen.
- Auto-update of the pack from GitHub releases.
- Lowest possible CPU use in DOS and Windows (idle, menus, players).
- Virtual CD behaving exactly like a real disc; run games from it as a drive.
- Feature parity between the DOS and Windows sides.
- Settings: every option adjustable from menus (SBSET romset by detected ROMs, quality, /VSF submenu).
- UniVBE / VESA on Mobility Radeon; ATI Rage Theatre acceleration (FPS counter chat).
- Port the Radeon 9700 OpenGL ICD to the ATI FireGL X1-256p.
- Redistributing shareware games from the user's disc (check licences first).

## Other projects in these chats

- dos-glide (repo deon81997/dos-glide): Glide-to-OpenGL wrapper for DOS (GLIDE2X.OVL, dgVoodoo 1.x / OpenGLide base), D810 target.
- PCIIRQ (repo deon81997/PCIIRQ): PCI IRQ routing tool for DOS and Win9x.
- 3dfx patch link scraper and auto-installer; DOS FPS counter driver; CONFIG.SYS editor with undo; AFUDOS GUI; DOS Ethernet kit for the D810; multi-agent AI setup.
