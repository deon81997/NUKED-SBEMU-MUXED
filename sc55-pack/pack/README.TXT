=============================================================================
  NUKED SB-EMU  -  SBEMU + Nuked-SC55 + HDPMI32i  -  COMPLETE GUIDE
  Hybrid pack: SC-55 / SoundFont MIDI, virtual CD drive, CD-Audio mixing,
  USB Xbox 360 pad as the gameport joystick
=============================================================================

Roland SC-55 (or any SoundFont) General MIDI for protected-mode DOS games,
through an emulated MPU-401 at port 330h, plus a virtual CD drive that mounts
.ISO / .CUE images and can play their CD-Audio tracks through your sound
card. Made for a Dell Latitude D810 (Pentium M, AC'97) running Windows 98,
but it works on any PC with a PCI sound card that SBEMU supports.

INSIDE WINDOWS 98 (DOS boxes and a Windows MIDI device): run SETUP98.BAT
and see README98.TXT.

CONTENTS
   1. What you get and what it does NOT do
   2. Quick start (5 minutes)
   3. Installing step by step
   4. Everyday commands
   5. SETTINGS.BAT (the settings screen)
   6. Using a SoundFont instead of the SC-55
  6b. Roland MT-32 / CM-32L (Munt)
   7. SBEMU command line options (complete list)
   8. Playing MIDI files and testing
   9. Virtual CD drive and disc images
  10. CD-Audio through your sound card (HYBRID, experimental)
 10b. USB in DOS: Xbox 360 pads on the gameport, USB keyboard and mouse
  11. Real-mode games: JEMM+QPIEMU or QEMM (optional, you supply the files)
 11b. 16-bit protected-mode games (Jazz Jackrabbit): SC55J16.BAT
  12. Using your real CD-ROM drive in DOS
  13. Optional: load the CD-ROM driver at every start (BOOTCFG / RESET)
  14. Cleaning up and uninstalling
  15. Troubleshooting
  16. File index
  17. Licences and credits


=============================================================================
 1. WHAT YOU GET AND WHAT IT DOES NOT DO
=============================================================================

  * An emulated Sound Blaster / OPL3 / MPU-401 (SBEMU) whose MIDI output is
    played either by a Roland SC-55 emulation (Nuked-SC55, needs YOUR OWN
    ROM dumps) or by a SoundFont (.sf2) of your choice.
  * HDPMI32i, the DOS protected-mode host SBEMU runs on.
  * Tools: settings screen, MIDI file player, MPU test tune, speed tests,
    CPU clock meter.
  * A virtual CD drive (uCDD, mount-only build) to mount .ISO / .CUE images,
    and an experimental mode that plays their CD-Audio tracks via SBEMU.
  * CD-ROM drivers (XCDROM, VIDE-CDD, LG GSCDROM) for a REAL drive in DOS.

  Limits:
  * SC55.BAT serves protected-mode games only (DOS4GW, DOS32A, DJGPP, ...); no
    memory manager is used. Real-mode games need JEMM+QPIEMU or QEMM, offered
    as separate optional launchers (section 11); you supply those files.
  * The SC-55 and MT-32 emulations need a Pentium Pro / Pentium II or newer
    (on a Pentium, Pentium MMX, K6 or Cyrix SBEMU says so and keeps the MPU
    silent; Sound Blaster, OPL3 FM, USB and the joystick work on any Pentium).
  * It runs in plain DOS. Windows 98 must be restarted in MS-DOS mode.
  * SBEMU cannot be unloaded. To stop it, restart the PC.
  * SETUP, CDINST and CLEANUP never edit CONFIG.SYS or AUTOEXEC.BAT. Nothing
    starts by itself. Only the optional BOOTCFG.BAT (section 13) changes boot
    files, and C:\RESET.BAT puts them back exactly.


=============================================================================
 2. QUICK START
=============================================================================

   1. Extract the zip. Copy the folder (NUKEDSB) to the hard disk.
   2. In a DOS window inside that folder:    SETUP
   3. (Optional, for disc images)            CDINST
   4. Restart in MS-DOS mode (Start > Shut Down > Restart in MS-DOS mode).
   5. Check the PC is fast enough:           C:\SC55\TEST.BAT
   6. Start the emulation:                   C:\SC55\SC55.BAT
   7. Hear it:                               C:\SC55\MPUTEST.EXE
   8. In your game choose General MIDI / MPU-401, port 330h (or the
      MPU-401 port you chose in SETTINGS; MENU shows it top right).

  Or do steps 5-7 (and everything else) from one screen: C:\SC55\MENU.BAT

  If the sound is too slow or stutters see section 15 (and try a SoundFont,
  section 6, which needs far less CPU than the SC-55 emulation).


=============================================================================
 3. INSTALLING STEP BY STEP
=============================================================================

  3.1  Extract the zip
       Use any unzip tool. Everything is inside one folder (NUKEDSB).
       Do NOT extract while an old CD driver is loaded under the device name
       XCDROM: Windows 98 refuses to write files in a folder called XCDROM
       then. If you did a BOOTCFG with an older pack, run C:\RESET.BAT and
       restart first.

  3.2  SETUP.BAT       (run it from the folder that contains FILES)

         SETUP            install to drive C:
         SETUP D          install to drive D: instead

       It copies files only:
         C:\SC55            SBEMU.EXE, SBSET.EXE, SC55WAV/SC55LOG/MPUTEST/
                            MIDPLAY/CPUMHZ.EXE, BOOTCFG.BAT, CLEANUP.BAT
         C:\SC55\ROMS       the ROM sets that were in the pack's FILES\ROMS
                            (SC-55 v1.21 = M1_*, SC-55mkII = M2_*)
         C:\SC55\SF2        empty folder for your SoundFonts
         C:\SC55\CDDRV      XCDROM.SYS, MSCDEX.EXE, VIDE-CDD.SYS
         C:\SC55\DOSMODE    ready-made CONFIG/AUTOEXEC samples + D810 notes
         C:\GSCDROM         LG CD-ROM driver
         C:\HDPMI           HDPMI32I.EXE
         C:\RESET.BAT       undo for BOOTCFG
       and it creates these small batch files in C:\SC55:
         MENU.BAT           one menu for everything (section 4)
         SC55.BAT           start SBEMU + SC-55 (or SoundFont)
         SETTINGS.BAT       the settings screen
         TEST.BAT           ROM boot + speed test
         CDROM.BAT          give the real CD-ROM a letter (finds the driver)
         CDROMLG.BAT        old name, just runs CDROM.BAT
       It needs about 20 MB free.

  3.3  CDINST.BAT      (optional: virtual CD drive and disc images)

         CDINST           install to drive C:
         CDINST D         install to drive D:

       Creates C:\CDSET (uCDD, SHSUCDX, menu, CDPLAY), C:\CDIMAGES (with a
       small test disc) and C:\MOUNT.BAT. HYBRID.BAT is in C:\CDSET.
       If C:\CDSET already exists, SETUP.BAT also refreshes it, so an older
       CD screen from an earlier pack can never be left behind.

  3.4  Your ROMs
       Your ROM dumps go in C:\SC55\ROMS. They are recognised by their
       content, so file names do not matter. Without ROMs use a SoundFont.


=============================================================================
 4. EVERYDAY COMMANDS
=============================================================================

  Run these from plain DOS (not from a Windows DOS box).

  C:\SC55\MENU.BAT
      One screen for everything. Menus on the left (Sound, Settings, Tests,
      Games, CD-ROM, Files), their items on the right, and below them the
      full description of the highlighted item. The top right shows whether
      SBEMU is loaded, how much of the CPU its MIDI synth is using (CPU nn%,
      green under 50%, yellow under 80%, red above), the MIDI synth and the
      Sound Blaster settings; the bottom line the GAMETEST results so far.
      The first time MENU starts (no saved settings yet) it offers the
      Automatic setup (Settings > Automatic setup, see section 5).
      SAFE MODE: hold a Shift key while MENU (or GAMETEST, SETTINGS, CD
      images) starts, or SET SC55SAFE=1, and the saved style, display mode
      and font are not used: plain 80x25 Classic. MENU then offers to reset
      them for good. Use it if a display mode or style leaves a screen you
      cannot read.
        Left/Right, Tab, PgUp/PgDn   choose a menu
        Up/Down, 1-6, Home/End        choose an item
        A-Z                           next item starting with that letter
        Enter                         run it          F1  help
        F2 (on a game)                play with your saved settings
        F3                            next style: Groovy, Classic, Sunset,
                                      Ocean, Neon, Candy, Midnight. Classic
                                      is the standard DOS colours with flat
                                      frames; the others have their own
                                      colours, 3D panels and an animated logo
        F4                            display mode (see below)
        F5                            MIDI player (MIDPLAY), from anywhere
        F6                            animation (logo colours, fades, opening
                                      windows) on / off
        F7                            letters: Terminus, Terminus bold or the
                                      VGA font
        F8                            the 3D menu (see below)
        Esc                           back to DOS
      3D MENU (optional; the text menu stays the normal one): F8, or
      Settings > 3D menu. A 640x480 VESA graphics version of the same menu,
      drawn in 3D by the CPU (no 3D card needed). F3 there picks the look: Panels (raised buttons), Carousel
      (a ring of cards) or Module (a rack sound module with an LCD, buttons
      and a knob). The keys are the same as in the text menu; F8 goes back
      to the text menu, Esc to DOS. F4 sets the speed: auto (the 3D scene
      at half resolution when the PC draws fewer than 15 pictures a
      second), full or half; the top right corner shows the frame rate for
      a few seconds. On a Pentium (not II) the 3D menu is slow even at half
      resolution: the text menu is the better choice there. MENU starts in 3D the next time if you
      left it in 3D (MENU3D.CFG); hold Shift while MENU starts for the text
      menu. Without a 640x480 VESA mode (no VESA BIOS / UNIVBE) MENU says
      so and stays in text.
      THE TEST PROGRAMS (JOYTEST, KMTEST, MPUTEST, USBTEST, TIMETEST) and
      MIDPLAY use the same style, letters and safe mode as MENU (the test
      programs also its display mode; F4 there changes it). With their
      output redirected (e.g. TIMETEST > FILE) the test programs write plain
      text instead.
      MOUSE (with a DOS mouse driver such as CTMOUSE loaded; a USB mouse
      through SBEMU works too): click a menu to open it, an item to choose
      it, the chosen item again to run it; the wheel moves up and down; a
      right click is "back" in questions, the file viewer and the display
      modes. In the 3D menu: click the menu tabs (and in Panels the items);
      in Carousel and Module click the middle to run the chosen item, the
      left or right side for the previous or next one.
      Nothing runs by itself. The menu writes your choice to MENUGO.BAT and
      exits; MENU.BAT runs it from plain DOS (SBEMU stays resident, so it
      must not be started from inside another program), then shows the
      menu again, on the item you last used.
      GAMES: Games > Add / edit games keeps your own list (GAMES.CFG): for
      each game its name, program (.EXE/.COM/.BAT, picked with a folder
      browser), arguments, SBEMU options used only while it runs (e.g.
      /VSCSET:mk1-v1.21 /K:22050, or /VMT32:MT32 for a game that wants an
      MT-32) and a CD image from C:\CDIMAGES that is mounted on drive M:
      before it starts. Up/Down choose, Enter edits (or adds a new game),
      Del removes one from the list (its files stay), Esc saves. The first
      time, the list is made from GAMETEST's games. A game whose program is
      gone is greyed out. The Games list scrolls past 6 entries; 1-9 jump.
      A diamond after a game means GAMETEST found the SBEMU settings that
      give it the best music timing. "Play" switches the loaded SBEMU to
      them (and to the game's own options), runs the game and switches back
      to your saved settings afterwards. F2 plays without GAMETEST's best.
      DISPLAY MODES (F4, also in GAMETEST and CD images): 80x25, 80x28,
      80x43, 80x50, the tweaked 480-line modes 80x30, 80x34, 80x60, every
      VESA text mode the video card offers (132x25, 132x43, 132x60 ...) and
      every 256-colour VESA graphics mode from 640x400 up (640x480, 800x600,
      1024x768, 1280x1024 ...). In text modes the screens stay 80x25 and are
      centred; in graphics modes they are drawn with the letters scaled to
      fill the screen. After switching you have 10 seconds to press Y to
      keep the mode; otherwise it goes back by itself (for a mode your
      screen cannot show). If a mode ever leaves the screen unusable,
      delete C:\SC55\THEME.CFG. Style, mode, animation and font are kept in
      THEME.CFG and used by MENU, GAMETEST, SETTINGS and CD images;
      programs started from the menu get the normal 80x25 DOS screen.
      FONT (F7): "Terminus" (the default) and "Bold" replace the letters
      and digits with Terminus, a pixel font made for screens, in the 8x16
      and 8x14 modes (the 8x8 modes keep the VGA font). Lines and blocks
      stay as they are, so frames join up. In VESA graphics modes each
      resolution gets the largest Terminus size that fits (10x20 at 800x600,
      12x24 at 1024x768, 16x32 at 1280x1024) drawn 1:1, so text stays sharp;
      with "VGA" the letters are enlarged by whole multiples. Text modes are
      stretched to the laptop's LCD by the video BIOS, which softens any
      font: a VESA graphics mode at the panel's own resolution (if the card
      lists it) gives the sharpest text.

  C:\SC55\SC55.BAT [options]
      Loads HDPMI, reads your saved settings (SBOPTS.TXT) and starts SBEMU
      with the emulated MPU-401. Up to four extra options can follow, e.g.
         SC55 /VSCGAIN:150        louder
         SC55 /VSCSET:mk2-v1.01   use the SC-55mkII ROM set
         SC55 /VCDA:100           with CD-Audio (needs the CD driver first)
      Then start your game.

  C:\SC55\SETTINGS.BAT
      Settings screen: Sound Blaster port/IRQ/DMA, MPU-401 port, SC-55 or
      SoundFont, ROM set, sample rate, FM core. See section 5.

  C:\SC55\TEST.BAT [options]
      Boots the emulated SC-55 from your ROMs, renders a demo, measures
      speed ("x real time"; you want 1.5x or better) and writes the report
      to C:\SC55\SC55LOG.TXT. Options: see SC55WAV below.

  C:\SC55\MPUTEST.EXE [/P:330] [/L]
      Plays a short test tune through the running SBEMU. /P: MPU port (hex),
      /L loop until a key is pressed.

  C:\SC55\MIDPLAY.EXE [/B] [/D:dir] [/L] [/R1] [/H] [/S] [/GM | /N] [file.mid ...]
      Full-screen MIDI player with visualizer, playlist and file explorer
      (section 8). With no file names it plays the MIDI files next to it.

  C:\SC55\CPUMHZ.EXE [/LOAD]
      Shows the real CPU clock speed, live (section 15).

  C:\SC55\SC55WAV.EXE rom-folder [out.wav] [/S:set] [/R:rate] [/G:gain]
                                 [/B:seconds] [/V] [/N]
  C:\SC55\SC55LOG.EXE  (same options, also writes SC55LOG.TXT)
      Stand-alone diagnostic that needs no sound card or SBEMU. Renders a
      demo of the SC-55 emulation to a WAV file and reports speed.
         /S:name   ROM set (default auto)    /R:rate   output rate (48000)
         /G:pct    gain in percent (100)     /B:sec    firmware boot (4)
         /V        speed graph in VGA mode   /N        no text graph

  C:\SC55\CDROM.BAT / CDROMLG.BAT
      Run MSCDEX for a REAL CD-ROM driver loaded in CONFIG.SYS and give it
      the letter D: (or the next free one). It finds the driver itself:
      XCDROM (XCD0001), VIDE-CDD (MSCD001), LG (MSCD000) or Oak (OAKCD), and
      says so if MSCDEX (or the virtual CD drive) is already loaded.

  C:\SC55\SC55JEM.BAT     real-mode games with JEMM + QPIEMU (section 11)
  C:\SC55\SC55QMM.BAT     real-mode games with QEMM (section 11)
  C:\SC55\CLEANUP.BAT      remove leftovers of old installs (section 14)
  C:\SC55\BOOTCFG.BAT      optional boot-file edit (section 13)
  C:\RESET.BAT             undo BOOTCFG exactly


=============================================================================
 5. SETTINGS.BAT  (THE SETTINGS SCREEN)
=============================================================================

  Run C:\SC55\SETTINGS.BAT (or MENU > SETTINGS). It loads HDPMI and starts
  SBSET.EXE, which scans the ROMs (takes a few seconds). Every SBEMU option
  the pack uses can be set here:

  Main window
     Sound Blaster     Port 220/240, IRQ 5/7/9, 8-bit DMA 0/1/3,
                       16-bit DMA 5/6/7, Type (1-3 SB, 4-5 SB Pro, 6 SB16)
     MIDI              MPU-401 port (330 usual), MIDI synth (SC-55,
                       SoundFont, MT-32 or CM-32L - see section 6b), and
                       the two sub-menus below
     Output            Sample rate 22050/32000/44100/48000 Hz, and the
                       "Timing & advanced" sub-menu
  SC-55 options        ROM set (/VSCSET), volume (/VSCGAIN), reverb/chorus
                       (/VSCFX), note limit (/VSCVOICES), speed control
                       (/VSCAUTO)
  SoundFont options    file (/VSF), voices (/VSFVOICES), reverb level, room,
                       damping, chorus level, speed, depth, song sends
  Volume               master (/VOL), sound effects (/VSBVOL), FM music
                       (/VOPLVOL), MIDI music (/VMIDIVOL), plus the SC-55
                       gain and CD-audio volume
  Timing & advanced    render-ahead (/VSCAHEAD), timer give-back (/VTIMER),
                       OPL on/off (/OPL), OPL core (/FMCORE), output jack
                       (/O), sample-rate fix (/FIXTC), CD-audio volume
                       (/VCDA), protected / real mode support (/PM /RM),
                       sound card / FM card / MPU card (/SC /SCFM /SCMPU),
                       MIDI out COM port (/MCOM)
  Not in the menus: /VMPU and /VSCROM (the launchers set them), the debug
  switches /DBG and /MDBG, and the one-off commands /SCL /COML /R.
  SETTINGS uses the style chosen in MENU (F3). The ROM sets are recognised
  from C:\SC55\ROMS\ROMHASH.CAC (made the first time), so SETTINGS, SBEMU
  and TEST.BAT start without re-reading every ROM; a ROM file that changes
  is checked again by itself.
  SETTINGS /PAGE:SC, /PAGE:SF, /PAGE:VOL or /PAGE:ADV opens a sub-menu
  (the MENU's SETTINGS entries do this).

  Keys
     Up / Down        choose a setting
     Left / Right     change the value
     Enter            open a drop-down list, a sub-menu, or the file
                      browser for the SoundFont file
     S or F2          save            D   restore defaults
     T                automatic setup (below)
     Esc              back / quit (asks to save if there are unsaved
                      changes)

  A red line warns about combinations that are too heavy for a laptop CPU
  (the SC-55 at 44.1 or 48 kHz) or that hurt game music (render-ahead
  off). Options that equal SBEMU's own default are left out of the saved
  line; SoundFont settings are only saved while the SoundFont is chosen,
  SC-55 settings only while the SC-55 is.

  The bottom of the window shows the exact SBEMU options that will be saved.
  They go to C:\SC55\SBOPTS.TXT (and SBOPTS.BAT for old launchers), which
  SC55.BAT and HYBRID.BAT pass to SBEMU as the file @SBOPTS.TXT, so they
  never fill the small DOS environment ("Out of environment space"). Restart
  SBEMU (restart the PC) after changing settings.

  AUTOMATIC SETUP (T, SETTINGS /TUNE, or MENU > Settings > Automatic
  setup; SBEMU must not be loaded): plays a busy test arrangement silently
  through each SC-55 ROM set you have (the original SC-55 and the mkII),
  with and without reverb/chorus, and shows how much of the CPU each needs.
  It then picks the best-sounding one that stays under half of the CPU (the
  game, the Sound Blaster and the FM synth need the rest), in this order:
  SC-55 with effects, mkII with effects, SC-55 without, mkII without. It
  uses 32000 Hz if there is plenty of room (under 35%), otherwise 22050 Hz,
  and a note limit of 16 above 40%. You see the numbers and choose with Y
  or N whether they are saved. Takes about a minute.

  Defaults: port 220, IRQ 7, DMA 1, 16-bit DMA 5, type 5 (SB Pro), MPU 330,
  ROM set auto, 22050 Hz, fast FM, render-ahead 50 ms, timer give-back on.

  Notes
  * SBEMU's MPU-401 is a UART port with no IRQ of its own, so only the port
    can be chosen.
  * Higher sample rates sound clearer but need more CPU.
  * If your game has its own Sound Blaster setup, set it to the same port,
    IRQ and DMA you chose here.


=============================================================================
 6. USING A SOUNDFONT INSTEAD OF THE SC-55   (experimental)
=============================================================================

  A SoundFont renders General MIDI with far less CPU than the SC-55
  emulation and needs no ROMs. It is not an SC-55, so it sounds different.

  In SETTINGS.BAT
     1. Set "MIDI synth" to "SoundFont (.sf2 file)".
     2. Press Enter on "SoundFont file". The browser lists folders and .sf2
        files (with sizes). Enter opens/chooses, Backspace goes up a folder,
        D picks a drive (C: to Z:), Esc cancels. It starts in C:\SC55\SF2.
     3. Press S to save, then start SC55.BAT.
     Switch "MIDI synth" back to "SC-55 emulation" to return.

  By hand
         SBEMU.EXE /VMPU /VSF:C:\SC55\SF2\NAME.SF2

  Reverb and chorus (SoundFont only; the SC-55 emulation has its own)
  A SoundFont has no built-in effects, so SBEMU can add a reverb and a chorus.
  In SETTINGS.BAT press Enter on "Effects" (with the synth set to SoundFont):
     Reverb level      Off, 10% ... 100%         (default Off)
     Reverb room size  20% ... 100%              (default 60%)  longer tail
     Reverb damping    0% ... 100%               (default 50%)  darker tail
     Chorus level      Off, 10% ... 100%         (default Off)
     Chorus speed      0.3 ... 3.0 Hz            (default 0.8 Hz)
     Chorus depth      0% ... 100%               (default 50%)
     Song sends        Follow the song (CC91/CC93) or one level for every
                       channel
  "Follow the song" scales each MIDI channel by the reverb (CC91) and chorus
  (CC93) amounts the song sends; GM songs usually set them. The level above
  is the overall amount. If a SoundFont already contains reverb, keep the
  level low or Off to avoid a doubled effect. The effects cost only a little
  CPU. By hand:
     SBEMU.EXE /VMPU /VSF:file.sf2 /VSFREV:40 /VSFCHO:20
  The effects are not yet tested on real hardware.

  Rules
  * Plain .sf2 files only (no .sf3 / compressed SoundFonts).
  * The path must be 8.3 with no spaces (rename the file if needed).
  * Memory use is about twice the file size plus 8 MB, so a 106 MB file
    needs roughly 220 MB of free RAM. Loading takes a while.
  * 64 voices. SBEMU prints "SoundFont file not found" or "could not be
    loaded" if something is wrong.
  * Not yet tested on real hardware.


=============================================================================
 6b. ROLAND MT-32 / CM-32L  (Munt emulation)
=============================================================================

  Many older games (Sierra, LucasArts, Origin, around 1987-1993) were written
  for the Roland MT-32, not for General MIDI: on an SC-55 their music plays
  with the wrong instruments. For those, SBEMU can be an MT-32 instead.

  1. Copy your MT-32 ROM dumps into C:\SC55\ROMS, next to the SC-55 ones:
     the control ROM (MT32_CONTROL.ROM, 64 KB, or two 32 KB halves) and the
     PCM ROM (MT32_PCM.ROM, 512 KB). For the CM-32L / LAPC-I: its control
     ROM (CM32L_CONTROL.ROM) and PCM ROM (CM32L_PCM.ROM, 1 MB). Names do
     not matter: they are recognised by content.
  2. SETTINGS > MIDI synth > "MT-32 emulation" or "CM-32L emulation", and
     save. Or only for one game: Games > Add / edit games > SBEMU options
     /VMT32:MT32. (Or by hand: SC55 /VMT32:MT32.)
  3. In the game's own setup choose "Roland MT-32" (or "LAPC-I" / "CM-32L"),
     port 330 (or the MPU-401 port you chose in SETTINGS).

  MT32 = the original MT-32 (what most early games expect), CM32L = the
  CM-32L / LAPC-I (more sound effects, used by later Sierra games), AUTO =
  the MT-32 if its ROMs are there, else the CM-32L. The MT-32 has no General
  MIDI: use the SC-55 for GM games. The SC-55 volume setting (/VSCGAIN) and
  MIDI music volume (/VMIDIVOL) also set the MT-32's volume. The emulation
  is lighter on the CPU than the SC-55. Its 20-character display shows in
  MIDPLAY's panel and in the Ctrl+Alt+F12 overlay (section 8).


=============================================================================
 7. SBEMU COMMAND LINE OPTIONS  (complete list)
=============================================================================

  SC55.BAT builds the line   SBEMU.EXE /VMPU /VSCROM:ROMS @SBOPTS.TXT ...
  (@file = read more options from that file; any SBEMU option can go in it)
  You can type extra options after SC55 (up to four) or run SBEMU.EXE by
  hand from C:\SC55 after loading   C:\HDPMI\HDPMI32I.EXE -r -x
  SBEMU.EXE /?  shows the built-in help.

  Sound Blaster
     /A:220 | /A:240       Sound Blaster base port (default 220)
     /I:5 | 7 | 9          IRQ (default 7)
     /D:0 | 1 | 3          8-bit DMA channel (default 1)
     /H:5 | 6 | 7          16-bit DMA channel (default 5)
     /T:1..6               1-3 Sound Blaster, 4-5 SB Pro (default 5), 6 SB16
     /OPL:0|1              OPL3 (FM) emulation on or off (default on)
     /FMCORE:0|1           0 = fast DOSBox OPL (default), 1 = accurate
                           Nuked OPL3 (heavier)
     /FIXTC:0|1            fix the time constant to match 11/22/44 kHz
     /VOL:n                master volume (default 80)
     /VSBVOL:pct           sound effects (digital Sound Blaster) volume,
                           0-100 (default 100), on top of the game's mixer
     /VOPLVOL:pct          FM (OPL / AdLib) volume, 0-100 (default 100)
     /VMIDIVOL:pct         General MIDI music volume, 0-400 (default 100).
                           SC-55: multiplies /VSCGAIN. SoundFont: scales
                           its master volume
     /K:rate               internal sample rate (default 22050; the settings
                           screen offers 22050, 32000, 44100, 48000)
     /O:0|1                output: 0 headphone, 1 speaker (Intel HDA) or
                           S/PDIF (Xonar DG)
     /PM:0|1               protected-mode support (needs HDPMI32I, default on)
     /RM:0|1               real-mode support (needs QEMM or JEMM; not used
                           by this pack)

  MPU-401 and MIDI
     /P:330                MPU-401 UART port
     /MCOM:n               send MIDI to a COM port instead (1-4, or a base
                           address; 9 = hardware MPU only)
     /VMPU[:0]             virtual MPU-401 on (SC-55 or SoundFont); /VMPU=0
                           turns it off. Also switched on by /VSCROM, /VSCSET
                           or /VSF.
     /VSCROM:folder        folder with the ROM dumps (default ROMS, i.e.
                           C:\SC55\ROMS when run from C:\SC55)
     /VSCSET:name          ROM set: auto, mk1-v1.21, mk2-v1.01 ... (SBEMU
                           lists the valid names in its help)
     /VSCGAIN:pct          SC-55 output gain, 0-1600 (100 = unity)
     /VSCVOICES:n          note limit (4-24; 0 = off, the default): at most n
                           notes held at once (drums not counted). A new
                           note past the limit first releases the oldest
                           one with a real Note Off, like voice stealing on
                           a real synth, so no note can get stuck. It saves
                           little CPU (about 5%): the SC-55's time goes into
                           its firmware, not its voices. Until beta.24 this
                           option skipped voices inside the synth instead,
                           which could freeze notes.
     /VSCFX:0|1            0 = skip the SC-55's reverb/chorus engine (faster,
                           dry sound). 1 = on (default)
     /VSCAUTO:0|1          1 = automatic speed control (default): if the CPU
                           cannot render the SC-55 in real time, SBEMU sheds
                           reverb/chorus, so games keep their
                           timer (otherwise their music runs slow). It adds
                           the load back when there is room. 0 = off
     /VSCAHEAD:ms          render the music this many ms ahead (default 50),
                           so each sound interrupt stays short and games keep
                           their timer. 0 = off (render inside each interrupt)
     /VTIMER:0|1           1 = give the game back the timer ticks it missed
                           while SBEMU was busy (default). Keeps the MIDI tempo
                           right in games that time music with a fast timer
                           (Doom, Duke 3D). 0 = off
     /VSFVOICES:n          SoundFont synth: most voices at once, 8-128
                           (default 64). Fewer = lighter on the CPU
     /VSF:file.sf2         play the MPU-401 with this SoundFont instead of
                           the SC-55 emulation
     /VMT32:AUTO|MT32|CM32L  play the MPU-401 with the Roland MT-32 / CM-32L
                           emulation (Munt) instead of the SC-55; its ROMs
                           go in the same ROMS folder (section 6b)
     /VSFREV:n             SoundFont reverb level, 0 (off) - 100
     /VSFROOM:n            reverb room size 0-100 (default 60)
     /VSFDAMP:n            reverb damping 0-100 (default 50)
     /VSFCHO:n             SoundFont chorus level, 0 (off) - 100
     /VSFCRATE:n           chorus speed in tenths of Hz, 1-30 (default 8)
     /VSFCDEPTH:n          chorus depth 0-100 (default 50)
     /VSFSEND:0|1          1 = follow the song's CC91/CC93 sends (default),
                           0 = same level on every channel

  CD-Audio
     /VCDA:pct             mix CD-Audio from the virtual CD (uCDD link):
                           0 off, 100 unity, 1-400 (section 10)

  Joystick (section 10b)
     /JOY:0|1              USB Xbox 360 pad as the gameport joystick at port
                           201h (default on; 0 if you use a USB keyboard or
                           mouse in DOS)
     /JOYAX:a,a,a,a        pad axes for joystick A X, A Y, B X, B Y: LX LY
                           RX RY LT RT TR (RT minus LT) or -; ~ in front
                           inverts (default LX,LY,RX,RY)
     /JOYBT:b,b,b,b        pad buttons for gameport buttons 1-4: A B X Y LB
                           RB LT RT ST BK LS RS GD DU DD DL DR or -
                           (default A,B,X,Y)
     /JOYKEY:b:code,...    pad buttons typed as keys, set-1 scan codes in
                           hex, E0xx for extended keys; - = none
                           (default ST:1C,BK:01 = Start Enter, Back Esc)
     /JOYDZ:pct            stick dead zone, 0-90 (default 10)

  Selection and diagnostics
     /SCL                  list installed sound cards
     /SC:n  /SCFM:n  /SCMPU:n   choose the card (all, FM only, MPU only)
     /R                    reset the sound card driver
     /COML                 list COM ports
     /DBG:n                debug output (0 console, 1-4 COM1-4, or address)
     /?                    help

  Messages at start-up
     "AC97 clock check: codec plays at xx% of the requested rate" shows
     whether the sound chip's clock was measured and corrected.
     "CD-Audio: uCDD link found" appears when /VCDA found the driver.


=============================================================================
 8. PLAYING MIDI FILES AND TESTING
=============================================================================

  After SC55.BAT is running:

     MIDPLAY.EXE                   play every MIDI file in the folder MIDPLAY.EXE is in
     MIDPLAY.EXE SONG.MID          play a file
     MIDPLAY.EXE *.MID             play a list (wildcards work)
     MIDPLAY.EXE /D:C:\MIDI        play every MIDI file in that folder
     MIDPLAY.EXE /B                open the file explorer to pick files
     MIDPLAY.EXE /L *.MID          repeat the whole list
     MIDPLAY.EXE /R1 SONG.MID      repeat one song
     MIDPLAY.EXE /H                shuffle
     MIDPLAY.EXE /S SONG.MID       plain one-line display instead of the full screen
     MIDPLAY.EXE /GM SONG.MID      send "GM System On" instead of a GS reset
     MIDPLAY.EXE /N SONG.MID       send no reset at all
     MIDPLAY.EXE /P:330 SONG.MID   MPU port (hex)

  Tip: copy your .MID files into C:\SC55 and just type MIDPLAY.

  SC-55 DISPLAY IN GAMES: press Ctrl+Alt+F12 while a game runs (or at the
  DOS prompt) and the module's real front panel appears in the top right
  corner: part, instrument, level, pan, reverb, chorus, key shift, MIDI
  channel and the level display (with the MT-32: its 20-character LCD).
  Ctrl+Alt+F12 again hides it. It works in text-mode screens and in
  320x200 256-colour games (VGA mode 13h, e.g. most early-90s games' menus
  and many games); games in mode-X (Doom) or VESA modes are left alone,
  since drawing there would garble their picture. The keys are only
  watched, never taken away from the game.

  Keys while playing:
     SPACE        pause / resume
     N  or END    next song              P   previous song
     LEFT/RIGHT   seek back / forward 10 seconds
     PGUP/PGDN    seek back / forward 60 seconds
     HOME         restart the song
     R            repeat: off, all, one  H   shuffle on/off
     L            repeat all on/off
     TAB          playlist: UP/DOWN/PGUP/PGDN pick, ENTER play it,
                  DEL remove it, TAB or ESC back
     B            file explorer: adds files to the playlist and plays the
                  first one added
     S            SC-55 front panel / channel table (remembered in
                  MIDPLAY.CFG). With SBEMU beta.34 and later the panel is
                  the emulated module's REAL display, read from SBEMU 10
                  times a second: part, instrument, level, pan, reverb,
                  chorus, key shift, MIDI channel, the 16 x 16 dot level
                  display and the synth's CPU load ("live from SBEMU").
                  With the MT-32 it shows the MT-32's 20-character LCD.
                  Without it (older SBEMU, SoundFont) the panel is worked
                  out from the MIDI file: part, instrument, level, pan,
                  expression, reverb, chorus, pitch bend, note, and 16
                  part level bars with peak hold
     - and +      choose the part shown on the panel (worked-out panel)
     ESC or Q     quit

  File explorer keys: UP/DOWN/PGUP/PGDN/HOME/END move, ENTER opens a folder
  or adds the file (or all marked files), SPACE marks files, A adds every
  MIDI file in the folder, BACKSPACE goes up a folder, D switches drive
  (C: and up; floppies are skipped), ESC closes it.
  Seeking replays instruments and controllers, so the sound stays right.
  Format 0 and 1 files and RMI are supported (.MID .MIDI .RMI .KAR).

  SC55.BAT (and SC55JEM / SC55QMM) save everything SBEMU prints to
  C:\SC55\SBLOG.TXT and then show it on screen. The file is rewritten on every
  run, so to report a problem, send that file (or a photo of the screen).
  To use this, run SETUP.BAT again so the launchers are regenerated.

  Note: MIDI/FM music used to run slow only while a game also played digital
  sound (SFX); SBEMU now keeps the synthesizers in real time in that case.

  MPUTEST.EXE plays a built-in tune. TEST.BAT checks the ROMs and speed
  without needing SBEMU at all.


=============================================================================
 9. VIRTUAL CD DRIVE AND DISC IMAGES
=============================================================================

  Run CDINST first (section 3.3). Put .ISO / .CUE / .MDM files with 8.3
  names in C:\CDIMAGES. The virtual drive is always drive M: (never A: or
  B:, those are the floppies).

  C:\CDSET\GUI.BAT  (or C:\MOUNT.BAT, or MENU > CD-ROM > CD images)
      The CD images screen, in the menu's style. The list shows every
      .CUE, .ISO, .MDM and loose .BIN with its size and disc label, and
      the folders inside C:\CDIMAGES (one folder per game works too).
      The panel on the right shows, without mounting the disc: its label
      and date, the tracks as a coloured strip (blue = data, yellow =
      CD music) with the length of the music, the INSTALL/SETUP program
      on the disc, and anything that needs fixing.
         Up/Down/PgUp/PgDn   choose          Enter   mount (or open a folder)
         Backspace           folder above    I       mount and run the
                                                     disc's INSTALL/SETUP
         C                   files on the disc
         U                   eject           F       fix the CUE (see below)
         S                   sort by name / size / type / label
         R                   look again      F1      all keys
         F3 / F4 / F6 / F7   style / display mode / animation / font
         Esc                 back to the menu
      Marks in the list: a note = CD music, ! = needs attention, I = has
      an installer, M: = in the drive now. Choosing another disc takes the
      old one out first. The screen remembers its folder, sort order and
      the selected disc (C:\CDSET\CDGUI.CFG).
      After a mount the screen comes back and says whether it worked (with
      uCDD's own message if it did not) and shows the disc label. With I,
      the installer runs from drive M: in plain DOS; install the game to
      drive C:, and the screen comes back when the installer ends.
      A CUE made on Windows usually names its BIN with a long name, e.g.
      FILE "Duke Nukem 3D (Track 1).bin". DOS only sees the short name
      (DUKENU~1.BIN), so uCDD cannot open it. Such a CUE gets a "!" in the
      list; F writes a corrected copy (DUKE3D_.CUE) that uses the short
      name. The original CUE is never changed. A CUE with several BIN
      files cannot be mounted by uCDD: join them first (e.g. binmerge).
      If uCDD says "The drive is locked or in use" when a disc is taken
      out, close the program that uses drive M:; if that does not help,
      restart the PC to change discs.

  C:\CDSET\CDIMG.BAT
         CDIMG                 list the images
         CDIMG NAME.ISO        mount it (CDIMG NAME also works: tries .ISO,
                               .CUE, .MDM)
         CDIMG /U              unmount
      A .CUE mounts its BIN with the audio tracks; a .MDM is a list of
      discs (see UCDD-DOC.TXT).

  C:\CDSET\CDON.BAT
      Turns the virtual drive on for this DOS session: loads uCDD, drops
      any old drive letter and gives the drive M: with SHSUCDX. GUI.BAT and
      CDIMG.BAT call it for you the first time. It sets the variable CDVM=1
      so it only runs once per session.
      Files on the CD are not marked read-only (SHSUCDX /R-), so game
      installers do not stop with "Access denied" on the files they copy.

  C:\CDSET\CDDIAG.BAT
      Runs the five loading steps one at a time with a pause between them,
      to see which step fails. Run it right after a restart and write down
      where an error appears.

  C:\CDSET\CDPLAY.COM drive: [track|STOP]
         CDPLAY M: 2            play track 2 (CD-Audio, see section 10)
         CDPLAY M: STOP         stop

  C:\CDSET\UCDD.EXE and SHSUCDX.COM are the driver and the drive-letter
  tool; see UCDD-DOC.TXT and SHSUCDX.TXT. SHSUCDX /U drops a drive letter.
  If M: says "Not ready" no image is mounted yet. uCDD is GPL-3.0.


=============================================================================
 10. CD-AUDIO THROUGH YOUR SOUND CARD  (HYBRID, experimental)
=============================================================================

  This pack's UCDD.EXE also reports CD-Audio requests (play, stop, seek,
  volume) in a small block of memory. SBEMU reads it, reads the audio
  sectors from the BIN file, converts 44.1 kHz to the card's rate and mixes
  them with the SC-55 and the game's sound. No memory manager is needed.

  Start with   C:\CDSET\HYBRID.BAT   instead of SC55.BAT. It loads the
  virtual CD first, then HDPMI and SBEMU with /VCDA:100.

  Test without a game:
         CDIMG TEST.CUE
         C:\CDSET\CDPLAY.COM M: 2      track 2 = both speakers
         (track 3 = left only, 4 = right only; track 1 is a data track)

  Limits: audio plays only from .CUE images with one BIN file and no PREGAP
  lines; mount with CDIMG or GUI (they pass a full path). While DOS itself
  is busy the track keeps playing from a 1.5-second buffer. If the PC hangs
  when a track starts, remove /VCDA from HYBRID.BAT.


=============================================================================
 10b. USB IN DOS: XBOX 360 PADS, USB KEYBOARD AND MOUSE  (experimental)
=============================================================================

  DOS games only know the analog PC joystick on the gameport (port 201h).
  Laptops such as the D810 have no gameport, so SBEMU can answer on port
  201h itself, with the sticks and buttons of a USB Xbox 360 pad.

  It is on by default: every start of SBEMU (SC55.BAT, SC55JEM.BAT ...)
  takes the USB ports over and sets up what is plugged in, all at once:
     - up to two Xbox 360 pads: pad 1 = joystick A, pad 2 = joystick B
       (two-player games), ring lit "player 1" / "player 2";
     - USB keyboards: typed through the PC's keyboard controller, so DOS
       and games see them like a PS/2 keyboard (keys repeat, lock LEDs
       follow);
     - a USB mouse: answers like a PS/2 mouse to a DOS mouse driver.
  With nothing plugged into USB it costs no time. SBEMU prints a line like
         Joystick (Port 201): enabled
           USB: 4 UHCI controller(s) (EHCI ports handed to companions),
                found: pad 1 keyboard mouse.
  Plug in or unplug any time, also while a game runs; it is picked up
  within a second (a mouse: see below). To leave USB to the BIOS instead:
  MENU > Settings > Joystick: USB pad = Off, Save.

  USB keyboard (/USBKB:1, default on): the BIOS's own USB keyboard support
  stops when SBEMU takes USB over, so SBEMU types the keys itself (as set 1
  scan codes through the 8042 keyboard controller). Needs a PC with a real
  keyboard controller (every PC with PS/2 ports; the D810 and P5PE-VM have
  one).

  USB mouse (/USBMS:1, default "auto"): a DOS mouse driver (CTMOUSE,
  MOUSE.COM) loaded AFTER SBEMU finds a PS/2 mouse - SBEMU answers its
  commands and sends the USB mouse's movement as PS/2 packets (the wheel
  too, for drivers that ask). Auto = only when a USB mouse is plugged in
  as SBEMU starts (a real PS/2 mouse is then not used); 2 = always; 0 =
  off. Test with MENU > Tests > Keyboard / mouse test (KMTEST).

  Test without a game:  MENU > Tests > Joystick test (JOYTEST), or run
  JOYTEST.EXE. Move the sticks: about 24 = left/up, 575 = centre and 1124
  = right/down (microseconds). Then calibrate the joystick in each game's
  setup as usual.

  What the game sees (Settings > Joystick, or the /JOY... options):
     Sticks       joystick A = left stick (the D-pad moves it too),
                  joystick B = right stick; or the triggers as an axis
                  (flight games: throttle / rudder), or only joystick A
     Buttons 1-4  A B X Y; or A X B Y; or RT A B X (trigger fires); or
                  A B LB RB. Button 1 is the usual fire button.
     Stick type   what the game is set up for (/JOYMODE):
                  Standard joystick (default)
                  CH Flightstick Pro - D-pad = 4-way hat, 4 buttons
                  Thrustmaster FCS   - D-pad = hat (on axis 4), 4 buttons
                     (flight sims, Descent, Wing Commander, X-Wing ...;
                      pick the same stick in the game's setup)
                  Gravis GamePad Pro (GrIP) - the digital mode: every
                     button (A B X Y, LB RB, LT RT, Start, Back, D-pad or
                     left stick), two pads; for games with GrIP support
                  Native - SBEMU leaves port 201h alone, so a real
                     gameport (sound card, Gravis / CH stick) works; USB
                     keyboard / mouse and pad keys stay on
                  Flight preset for Sticks: right X = rudder, RT = throttle.
                  JOYTEST /GRIP shows the GrIP buttons.
     Two pads     pad 1 = joystick A, pad 2 = joystick B, each with the
                  first two Sticks axes and the first two Buttons below
                  (left stick, A + B by default). One pad: all four.
     Keys         Start = Enter, Back = Esc, so menus work from the pad;
                  optional LB = Space, RB = Ctrl, or the D-pad as arrow
                  keys. Typed like real keys (games with their own
                  keyboard handler see them too).
     Dead zone    stick movement ignored around the centre (default 10%).

  Works with: the wired Microsoft Xbox 360 pad and wired pads that use the
  same protocol (Hori, Mad Catz, Logitech ChillStream, Saitek ... the
  pads in the Windows 98 x360c driver). The wireless receiver for PC may
  work (two pads; not tested). Keyboards / mice: any USB keyboard, and
  mice that offer the standard "boot" mode (nearly all); wireless
  keyboard + mouse receivers too. Not: Xbox One / Series pads, other
  USB joysticks. A USB hub in between is fine (up to four deep).
  USB controllers - all PC kinds; the pad can go in any port of any of them:
     UHCI  Intel (ICH - ICH10, the D810's ICH6) and VIA chipsets
     OHCI  NEC uPD720100 / uPD720101 USB 2.0 PCI cards, SiS, ALi, nVidia
           and AMD chipsets
     EHCI  (USB 2.0) with UHCI / OHCI companions: every port is handed to
           them. Without (Intel 5-series and later, a hub built into each
           port): driven directly, the pad reached through that hub.
     xHCI  (USB 3) Intel 7-series and later, NEC/Renesas uPD720200/201/202
           cards, ASMedia, VIA, Fresco, AMD: its USB 2 ports are used (USB 3
           ones are left off), and Intel's shared ports are moved over from
           the EHCI part.
  Not supported: CardBus cards (the BIOS leaves the PC Card slot switched
  off in DOS) and Renesas uPD720201/202 cards without their firmware ROM.

  Good to know
     * While SBEMU runs with USB on, it owns the USB controllers: the BIOS's
       USB keyboard / mouse support stops until the next restart; SBEMU's
       own keyboard / mouse support takes over (the D810's own keyboard
       and touchpad are not USB). Don't use it together with USBDDOS or
       another DOS USB driver.
     * USB mouse: load the mouse driver after SBEMU (put it after SC55.BAT
       in your batch file). If the driver says no mouse found, check that
       the USB mouse was plugged in when SBEMU started (or use /USBMS:2).
     * It works for protected-mode games (HDPMI) and, with SC55JEM.BAT or
       SC55QMM.BAT, real-mode games too - the same as the sound.
     * Each read of port 201h is a trap, so a game polling the stick in a
       tight loop sees slightly coarser steps than with a real joystick;
       calibration takes care of it.
     * Not under Windows 98: Windows has its own driver for the pad.
     * If the pad is not found: restart the PC (without SBEMU) and run
       USBTEST.EXE from C:\SC55 (or MENU > Tests > USB pad finder). It
       uses the same USB code as SBEMU and shows every step: which USB
       controllers it found and what it did with each, what is plugged in,
       then the pad live and every plug / unplug. USBTEST USBTEST.TXT also
       writes it all to USBTEST.TXT - send that with a bug report. It shows
       USB keyboards and mice too (keys pressed, mouse movement). Esc quits
       (PS/2 or USB keyboard).


=============================================================================
 11. REAL-MODE GAMES: JEMM+QPIEMU OR QEMM  (optional, you supply the files)
=============================================================================

  SC55.BAT (HDPMI only) serves PROTECTED-MODE games. Most older real-mode DOS
  games need a V86 memory manager that can trap I/O ports. Two are supported
  as separate options.

     Option A   JEMM + QPIEMU.DLL     start with  C:\SC55\SC55JEM.BAT
        JEMM 5.86 is INCLUDED (installed to C:\SC55\RM\JEMM): JEMMEX.EXE,
        JEMM386.EXE, JLOAD.EXE, QPIEMU.DLL and Jemm's own texts.
     Option B   QEMM                  start with  C:\SC55\SC55QMM.BAT
        QEMM is NOT included (commercial). Put QEMM386.SYS and the other files
        your QEMM version needs in the pack's FILES\QEMM folder before SETUP
        (SETUP copies them), or copy them to C:\SC55\RM\QEMM.
  The two launchers do the same as SC55.BAT (HDPMI, your saved SBOPTS settings,
  SBEMU with the SC-55 or SoundFont); SC55JEM.BAT first loads QPIEMU.DLL with
  JLOAD.

  The memory manager itself must be loaded by CONFIG.SYS. A manager in the real
  CONFIG.SYS stops Windows 98 from starting normally, so use a private
  MS-DOS-mode shortcut (no boot file is changed). Step by step, with the
  ready-made lines CONFIG.JEM / AUTOEXEC.JEM (JEMM) and CONFIG.QMM / AUTOEXEC.QMM
  (QEMM), is in
     C:\SC55\DOSMODE\REALMODE.TXT
  then at the DOS prompt run SC55JEM.BAT (or SC55QMM.BAT) and start the game.

  Notes
  * Do not run SC55.BAT and a launcher in the same session. SBEMU cannot be
    unloaded; restart to change.
  * JEMM: NOEMS turns EMS off, X2MAX=8192 limits XMS to 8 MB, and
    JEMMEX NOVCPI (after SBEMU is loaded) makes DPMI games that would choose
    VCPI work. Details are in REALMODE.TXT.
  * Not tested on real hardware.


=============================================================================
 11b. 16-BIT PROTECTED-MODE GAMES (JAZZ JACKRABBIT)  (experimental)
=============================================================================

  A few games run as 16-bit DPMI programs (Borland Pascal / C++ "16-bit
  protected mode", e.g. Jazz Jackrabbit). They need a 16-bit DPMI host,
  and neither SC55.BAT nor SC55JEM.BAT gives their Sound Blaster, OPL,
  MPU-401 or joystick ports to SBEMU. Use this launcher instead:

     C:\SC55\SC55J16.BAT       (JEMMEX loaded from CONFIG.SYS, as for
                                SC55JEM.BAT - section 11)

  It loads QPIEMU, then C:\HDPMI\HDPMI16I.EXE (16-bit HDPMI that can trap
  ports), HDPMI32I, SBEMU with your settings, and SBEMU16.COM. SBEMU16
  stays resident (about 3 KB) and sends every access a 16-bit game makes to
  the SB / OPL / MPU / PIC / DMA ports and the gameport through to SBEMU,
  so the game sees the same emulated card as a real-mode game. The
  joystick is answered locally from the stick position SBEMU gives it, so
  calibration is stable.

  Notes
  * HDPMI16I must be loaded before HDPMI32I, and only this HDPMI16I works
    with the pack's HDPMI32I (another HDPMI16 stops with "DPMI detected").
  * 32-bit protected-mode games and real-mode games keep working as with
    SC55JEM.BAT.
  * Jazz Jackrabbit: choose "Sound Blaster Pro" in SETUP.EXE. If the CD
    version stops with "Runtime error 200", it needs the Turbo Pascal CRT
    fix for fast CPUs (any "TPPATCH" style patch for FILE0001.EXE).
    Check MUSIC / SOUND FX in its options are not turned all the way down.
  * Tested in QEMU (Jazz: sound, music, joystick calibration and menus).
    Not yet tested on real hardware.


=============================================================================
 12. USING YOUR REAL CD-ROM DRIVE IN DOS  (Dell D810 etc.)
=============================================================================

  Drivers in C:\SC55\CDDRV and C:\GSCDROM:
     XCDROM.SYS     IDE/ATAPI driver (device name XCD0001). The pack uses
                    /UX = UltraDMA off (PIO). Remove /UX to try DMA.
                    Add /SM (secondary master), /PM, /PS or /SS to pick
                    the unit if it finds no drive.
     VIDE-CDD.SYS   PIO only, /P:170,15 = secondary channel
     GSCDROM.SYS    LG drives (device name MSCD000)
     MSCDEX.EXE     gives the drive a letter (CDROM.BAT does this)

  Real drive + virtual CD drive together: MSCDEX and SHSUCDX (the virtual
  drive M:) cannot both be loaded the usual way. CDON.BAT handles it: if
  MSCDEX is already loaded it installs SHSUCDX next to it, and if only the
  CD-ROM driver is loaded SHSUCDX gives the real drive its letter (D:) too.
  M: needs LASTDRIVE=M or higher in CONFIG.SYS; the samples, BOOTCFG and
  JEMMCFG add LASTDRIVE=Z.

  The safe way (no boot file changes) is a private MS-DOS-mode shortcut;
  step by step with CONFIG/AUTOEXEC examples in
     C:\SC55\DOSMODE\CDROM-D810.TXT
  Samples: CONFIG.XCD / AUTOEXEC.XCD (XCDROM), .VID (VIDE-CDD), .LG (LG),
  .OAK (Oak driver; you must add OAKCDROM.SYS yourself).

  Why: a DOS driver in the real CONFIG.SYS makes Windows 98 fall back to
  slow MS-DOS compatibility mode on that IDE channel.


=============================================================================
 13. OPTIONAL: CD-ROM DRIVER AT EVERY START (BOOTCFG / RESET)
=============================================================================

  Only if you want it. SETUP never does this by itself.

         C:\SC55\BOOTCFG.BAT X     XCDROM  (UltraDMA off)
         C:\SC55\BOOTCFG.BAT V     VIDE-CDD
         C:\SC55\BOOTCFG.BAT L     LG GSCDROM

  It saves your boot files as C:\CONFIG.PRE and C:\AUTOEXEC.PRE, then adds
  the driver to CONFIG.SYS and MSCDEX to AUTOEXEC.BAT. There is no menu. It
  refuses to run twice without RESET.BAT in between. Windows 98 starts as
  usual; for a DOS prompt with the CD-ROM hold CTRL (or F8) at boot and
  choose "Command prompt only".

  UNDO:        C:\RESET.BAT   puts both files back exactly.

  JEMMEX AT START (for real-mode games):
         C:\SC55\JEMMCFG.BAT        JEMMEX only
         C:\SC55\JEMMCFG.BAT X      JEMMEX + XCDROM CD-ROM
         C:\SC55\JEMMCFG.BAT V      JEMMEX + VIDE-CDD CD-ROM
  Adds a start menu (5 s): 1 = Windows 98 (default, your old files
  unchanged), 2 = DOS with JEMMEX. Windows cannot run with JEMM loaded,
  so choice 2 stops at a DOS prompt: type SC55JEM, then start the game.
  Same backups and the same undo (C:\RESET.BAT) as BOOTCFG; run only one
  of the two (RESET.BAT in between to switch).
  Windows 98 will not start: F8 at boot, "Command prompt only", then run
  C:\RESET.BAT.


=============================================================================
 14. CLEANING UP AND UNINSTALLING
=============================================================================

  CLEANUP.BAT  (in the pack folder and in C:\SC55)
      Lists leftovers of earlier versions (old JEMMEX, UCDD, OAKCD and
      XCDROM folders, stray temp files), asks once, then deletes only
      those. It never edits or deletes CONFIG.SYS, AUTOEXEC.BAT or their
      backups, only shows related lines it finds. Your settings, ROMs and
      images are never touched. Afterwards run SETUP.BAT and CDINST.BAT
      again.

  UNINSTAL.BAT  (in the pack folder and in C:\SC55)
      Puts the PC back the way it was before the pack. Run it from the DOS
      prompt (not from MENU), best right after a restart:
         C:\SC55\UNINSTAL          (UNINSTAL D  if you installed to D:)
      It lists what it found and asks before changing anything. Then:
      1. If BOOTCFG.BAT or JEMMCFG.BAT changed CONFIG.SYS / AUTOEXEC.BAT,
         they are put back from the backups those made (like RESET.BAT)
         and checked. If that fails, it stops and removes nothing else.
      2. K = keep your own files: ROMs, SoundFonts and settings are moved
         to C:\SC55SAVE, your disc images stay in C:\CDIMAGES.
         D = delete them too, for exactly the state before the install.
      3. It removes every folder and file the pack created: C:\SC55 (with
         ROMS, SF2, CDDRV, DOSMODE, RM, logs and settings), C:\CDSET,
         C:\CDIMAGES, HDPMI32I/16I.EXE in C:\HDPMI, the LG driver in
         C:\GSCDROM, C:\MOUNT.BAT, C:\RESET.BAT, and leftovers of older
         packs (CLEANUP's list). C:\HDPMI, C:\GSCDROM and C:\CDIMAGES are
         only removed if nothing of yours is left in them.
      Nothing else was ever installed: no Windows settings, registry or
      MSDOS.SYS changes. The pack folder you extracted is left alone;
      delete it yourself. A "Batch file missing" line at the very end is
      normal (the last helper deletes itself).

  If an EARLIER version of the setup changed your boot files, restore the
  backups it made:  COPY C:\CONFIG.SC5 C:\CONFIG.SYS  and
  COPY C:\AUTOEXEC.SC5 C:\AUTOEXEC.BAT


=============================================================================
 15. TROUBLESHOOTING
=============================================================================

  REPORTING A PROBLEM: MENU > Files > Make a bug report (or BUGREP in
  C:\SC55). It writes C:\SC55\BUGREP.TXT with everything needed to find
  the cause: CONFIG.SYS, AUTOEXEC.BAT, MSDOS.SYS, the environment, MEM /C,
  your settings, SBEMU's messages (SBLOG.TXT), SBEMU's start-up details
  (SBDETAIL.TXT), test results, the Windows 98 log and the file lists
  (ROMs by name only). Nothing is changed or sent anywhere. Run SC55 once
  first, so SBLOG.TXT and SBDETAIL.TXT are from the problem.

  SBDETAIL.TXT is written every time SC55.BAT starts SBEMU: the command
  line, every option and whether you set it, DOS and Windows versions,
  the CPU, every PCI sound device with its IRQ, the card SBEMU chose,
  each start-up step ("[step] ...") and every port trap. Each line is
  saved at once, so if the PC hangs while SBEMU starts, the last
  "[step]" line shows where. SET SBEMUDETAIL=OFF turns it off.

  SBEMU SAYS "Not loaded" IN THE MENU: the line under the logo shows
  the first error from SBLOG.TXT.

  MUSIC SLOWER THAN IT SHOULD BE IN A GAME (but fine in MIDPLAY): run SBDIAG.
      SC55                start SBEMU as usual
      SBDIAG START        start measuring
      (play the game a minute or two with music, then quit it)
      SBDIAG              report on screen and in C:\SC55\SBDIAG.TXT
  It lists the reasons, most important first: lost timer ticks, how
  long the sound interrupt blocks the timer, synth load, the game's own
  sound handler, port traps, dropped MIDI. SBDIAG does not touch the
  timer or the sound card, so it is safe to run.

  GAMETEST - runs your games with SBDIAG automatically (menu program):
      SC55                start SBEMU as usual
      GAMETEST            pick games, test length and SBEMU options, then Run
      GAMETEST /RUN       run the ticked tests right away, no questions
  - Any Sound Blaster port / IRQ / DMA and any MPU-401 port work: before
    each test GAMETEST sets the game's own sound setup to what SBEMU is
    using (BLASTER and the port its MPU-401 answers at). Doom-engine games
    (DEFAULT.CFG: snd_sbport, snd_sbirq, snd_sbdma, snd_mport) and Apogee /
    Build games like Duke 3D (*.CFG: BlasterAddress, BlasterInterrupt,
    BlasterDma8, BlasterDma16, MidiPort) are done automatically; only those
    lines change, never the music or sound device chosen. The first time
    a file is changed the original is kept as <name>.GTB. Other games: set
    the values GAMETEST shows at the bottom in the game's own setup.
  - Doom / Doom II play their demo (-playdemo demo1) and quit by themselves.
  - Duke 3D runs unattended too: it plays its own built-in demos (with
    the level music), and when the test time is up SBEMU types the quit
    keys into it (default  ESC W3 Q W2 Y W3 Y W3 ESC W3 Q W2 Y W3 Y :
    Esc opens the main menu over the demo, Q asks "Are you sure you want
    to quit?", Y says yes, Y closes the "Play on TEN" screen; the second
    round covers keys that arrived while the logo or title was still on
    screen). No Enter on purpose: in a menu it would start a new game.
    Tested in an emulator with Duke 3D Atomic 1.5, from the logo, the
    title and the demo. SBEMU holds each typed key for half a second:
    Duke looks at the keyboard once per frame, and the short presses of
    earlier versions could fall between two frames and get lost. If you
    changed Duke's quit keys by hand, set them to the line above.
    Change the keys per game in the menu with S (start keys) and Q (quit
    keys). Words: W<seconds> (wait, e.g. W2 or W0.5), ESC ENTER SPACE TAB
    BKSP CTRL SHIFT ALT UP DOWN LEFT RIGHT F1-F12 A-Z 0-9. A game with no
    quit keys has to be quit by hand. Keep your hands off the keyboard
    during a run. Keys left over once the game has quit are cancelled.
  - The results screen colours each test by its music clock: green
    99-100% (right tempo), yellow 95-99%, red below 95% or locked up.
  - Best settings: after each test GAMETEST remembers, per game, the
    options whose music clock is closest to 100% (GTBEST.CFG; about as
    close -> the one nearer the defaults wins). The menu shows "best 99.6%"
    next to each game, "Best settings per game" lists them, and MENU's
    Play uses them. "Clear all results" starts over (do it after you
    change SBEMU versions or sound settings).
  - "Test ALL options" runs every combination of render-ahead, timer
    give-back, SC-55 reverb/chorus and speed control for every game.
  - Results: GAMETEST.TXT (one line per test: music clock %, longest
    interrupt, synth load, silence, dropped MIDI) and GTFULL.TXT (the
    full SBDIAG report of every test).
  - If a test locks up the PC, restart, run SC55 and GAMETEST again: it
    notes which test it was in GAMETEST.TXT and offers to carry on.
  Set each game to General MIDI (330) and Sound Blaster 220/7/1 first.

  Sound plays too slowly (about 90-95%)
     * Look at the "AC97 clock check" line SBEMU prints at start. It says if
       the codec's real clock differs from the requested rate and corrects
       for it.
     * Run C:\SC55\CPUMHZ.EXE. If the speed is far below your CPU's rating
       (SpeedStep in plain DOS), use AC power and set the BIOS to maximum
       performance. CPUMHZ /LOAD keeps the CPU busy so you can watch whether
       it ramps up.
     * Try another sample rate in SETTINGS.BAT (48000 Hz is the AC'97
       native rate).
     * If MIDI sounds right in MIDPLAY but wrong in games, tell the author
       which game and whether the pitch is also low.

  Music stutters or the PC is too slow
     * Run TEST.BAT. Under 1.5x real time is too slow for the SC-55.
       The emulated chip costs the same at any sample rate, so lowering the
       rate does not help the SC-55 (it does help SoundFonts).
     * Try without reverb/chorus and compare the "x real time" figure:
          TEST.BAT /NOFX
       If it helps, set it in SETTINGS > SC-55 options (Reverb/chorus) or add /VSCFX:0 to
       SC55.BAT. The core already skips work exactly (timers, idle
       voices), with identical sound. A note limit (/VSCVOICES) is safe
       but saves only about 5%.
     * Or use a SoundFont (section 6): much lighter than the SC-55. A small
       General MIDI .sf2 (4-30 MB) with /VSFVOICES:32 is the easiest on the CPU.

  "SBEMU: cannot find sound card"
     SBEMU supports PCI cards only (AC'97, HDA, SB Live and others).
     SBEMU.EXE /SCL lists what it sees.

  HDPMI not found / "DPMI host" errors
     Run C:\HDPMI\HDPMI32I.EXE -r -x first (the batch files do this).

  "Device driver not found: 'XCDROM'" or file errors in a folder named XCDROM
     The CD driver's device name now is XCD0001. After an update run
     C:\RESET.BAT, restart, extract the new pack and (if wanted) run
     BOOTCFG X again.

  Virtual CD: "General failure reading drive" / "Not ready reading drive M"
     Not ready = no image mounted. Mount one with CDIMG or GUI. If loading
     fails run C:\CDSET\CDDIAG.BAT after a restart and note the failing step.

  Windows 98 will not start after BOOTCFG
     F8 > Command prompt only > C:\RESET.BAT.

  Batch file quirks (Windows 98 COMMAND.COM)
     Run them from plain DOS or an MS-DOS prompt, not from Explorer. For a
     batch file that misbehaves:  COMMAND /Y /C file.bat  steps through it.


=============================================================================
 16. FILE INDEX
=============================================================================

  Pack folder
     SETUP.BAT        installs SBEMU, SC-55 tools, HDPMI, CD drivers
     CDINST.BAT       installs the virtual CD drive and disc menu
     CLEANUP.BAT      removes leftovers of old installs
     UNINSTAL.BAT     removes the whole pack, restores the boot files
     README.TXT       this guide
     FILES\SC55       SBEMU.EXE, SBSET.EXE, SC55WAV.EXE, SC55LOG.EXE,
                      MPUTEST.EXE, MIDPLAY.EXE, CPUMHZ.EXE, SC55MENU.EXE,
                      SBDIAG.EXE, GAMETEST.EXE, TIMETEST.EXE, JOYTEST.EXE,
                      USBTEST.EXE, KMTEST.EXE, SBEMU16.COM
     FILES\ROMS       ROM dumps (yours)
     FILES\HDPMI      HDPMI32I.EXE, HDPMI16I.EXE (16-bit, section 11b)
     FILES\CDSET      uCDD, SHSUCDX, CDGUI, CDPLAY, GUI/CDIMG/CDON/CDDIAG
     FILES\CDIMAGES   TEST.CUE / TEST.BIN test disc
     FILES\BOOT       BOOTCFG.BAT, RESET.BAT
     FILES\DOSMODE    CONFIG/AUTOEXEC samples, CDROM-D810.TXT
     FILES\JEMM      JEMM 5.86 (JEMMEX, JEMM386, JLOAD, QPIEMU.DLL)
     FILES\QEMM      empty: for your own QEMM files
     FILES\XCDROM, VIDECD, GSCDROM   CD-ROM drivers and their texts

  Installed
     C:\SC55\MENU.BAT   the menu (runs SC55MENU.EXE; MENUGO.BAT is its
                        short-lived "what to run next" file)
     C:\SC55\SC55.BAT TEST.BAT SETTINGS.BAT CDROM.BAT CDROMLG.BAT
     C:\SC55\SC55JEM.BAT SC55QMM.BAT   real-mode launchers (section 11)
     C:\SC55\SC55J16.BAT   + 16-bit protected-mode games (section 11b)
     C:\SC55\RM\JEMM (Jemm 5.86), C:\SC55\RM\QEMM (your files)
     C:\SC55\THEME.CFG    menu style, display mode, animation and font
     C:\SC55\MENU.CFG     the item the menu was last on
     C:\SC55\ROMS\ROMHASH.CAC  ROM recognition cache (safe to delete)
     C:\SC55\SBOPTS.TXT   your saved settings (made by SETTINGS.BAT; also
                          SBOPTS.BAT, the same as a SET line)
     C:\SC55\SC55LOG.TXT  report written by TEST.BAT
     C:\CDSET\HYBRID.BAT  CD drive + SC-55 + CD-Audio
     C:\SC55\CDCHK.COM, C:\CDSET\CDCHK.COM   13-byte helper: is MSCDEX/SHSUCDX
                          loaded? (used by CDROM.BAT and CDON.BAT)
     C:\MOUNT.BAT         shortcut to the disc menu


=============================================================================
 17. LICENCES AND CREDITS
=============================================================================

  SBEMU (Baron-von-Riedesel, crazii, others), Nuked-SC55 and this port:
  GPL-2.0-or-later. Nuked-OPL3: LGPL-2.1+. TinySoundFont: MIT.
  Munt libmt32emu 2.8.3 (MT-32 emulation, Dean Beeler, Jerome Fisher,
  Sergey V. Mikayev and others): LGPL-2.1+ (C:\SC55\MUNT-LIC.TXT), source
  at https://github.com/munt/munt.
  The screen font Terminus (Dimitar Zhekov): SIL Open Font Licence 1.1.
  USB joystick: written for this pack (GPL-2.0); xHCI bring-up after the DOS
  tool of xhci98 (Yeo Kheng Meng, GPL-2.0); UHCI / OHCI handling after
  USBDDOS (crazii, GPL-2.0), the pad's report layout after Linux xpad.c.
  Xbox and Xbox 360 are trademarks of Microsoft.
  HDPMI32i is part of HX (japheth). Jemm, JLoad and QPIEMU (japheth): see
  JEMMLIC.TXT. uCDD: GPL-3.0 (UCDD-LIC.TXT).
  XCDROM: GPL (source in FILES\XCDROM). SHSUCDX, VIDE-CDD, GSCDROM and
  MSCDEX belong to their authors.
  Roland, SC-55, MT-32 and CM-32L are trademarks of their owners. The ROM dumps are your
  own; SoundFonts have their own licences.
