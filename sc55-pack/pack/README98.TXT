=============================================================================
  NUKED SB-EMU FOR WINDOWS 98
  Sound Blaster / OPL3 / MPU-401 for DOS boxes + "NUKED SB-EMU" MIDI device
=============================================================================

The DOS pack plays the SC-55 when you boot to DOS. This part does the same
inside Windows 98:

  * DOS programs in a DOS box (a window or full screen) get a Sound Blaster
    (1.5 / 2.0 / Pro / Pro 2 / 16, as you choose), OPL3 FM music at 388h and
    an MPU-401 at 330h. Their MIDI music is played by the SC-55 (or the
    MT-32 or a SoundFont), the FM music by the DOSBox or Nuked OPL3 core,
    and the digital sound effects through your sound card.
  * Windows programs (Media Player, MIDI sequencers, Windows games) get a
    MIDI device called "NUKED SB-EMU" played by the same synth.
  * The settings are the DOS pack's: C:\SC55\SBOPTS.TXT. What you set in
    DOS (SETTINGS.BAT) applies in Windows and the other way round.

It is made for the Dell Latitude D810 with the SigmaTel AC'97 WDM driver,
but works with any sound card that has a Windows 98 driver.

It does NOT replace "Restart in MS-DOS mode" for games that need it (games
that want all of the memory, or that do not run in a DOS box): use the DOS
pack there, as before.


-----------------------------------------------------------------------------
 1. INSTALLING
-----------------------------------------------------------------------------

  1. Install your sound card's Windows 98 driver first (for the D810: the
     SigmaTel driver). Windows must be able to play a .WAV file.
  2. Copy the pack folder to the hard disk (or use it from CD) and run
     SETUP98.BAT, or FILES\WIN98\SC55W.EXE /INSTALL.
  3. The setup window:
       Pack folder      C:\SC55 (where the DOS pack's SETUP.BAT put it).
                        The Windows files go to C:\SC55\WIN98. ROMs that
                        are not in C:\SC55\ROMS yet are copied there.
       Start with Windows
                        leave on: the DOS sound and the MIDI device only
                        work while SC55W.EXE runs.
       Make "NUKED SB-EMU" the default MIDI device
                        Windows programs that use the MIDI Mapper (most do)
                        then play through the SC-55.
       Turn off Windows' own Sound Blaster emulation (SBEMUL)
                        Windows 98 SE has its own (basic) Sound Blaster
                        emulation for WDM sound drivers. It holds port 220
                        and must be off for this one. Greyed out if your
                        Windows does not have it.
       Set BLASTER= in AUTOEXEC.BAT
                        so DOS programs find the Sound Blaster. It is kept
                        up to date when you change the settings.
  4. Restart Windows (Setup asks). A keyboard icon appears next to the
     clock. It takes a few seconds after Windows starts before the SC-55
     is ready.

Nothing in CONFIG.SYS is changed. Setup notes everything it changes in
C:\SC55\WIN98\SC55W.INI and the uninstaller puts it back.


-----------------------------------------------------------------------------
 2. USING IT
-----------------------------------------------------------------------------

  Tray icon (next to the clock), right-click:
     Status...        what is running, any problems, the CPU load, and
                      counters (MIDI bytes, FM writes, Sound Blaster
                      interrupts). Double-click the icon for the same.
     Settings...      see section 3.
     All notes off    stops stuck notes.
     Exit             stops the program (DOS boxes lose their sound, the
                      MIDI device stops working).

  DOS games: start them in a DOS box as usual (double-click, or from an
  MS-DOS Prompt). In the game's setup choose
     Sound effects:   Sound Blaster 16 (or Pro / 2.0, whatever you set),
                      port 220, IRQ 5 or 7, DMA 1, 16-bit DMA 5 - as shown
                      in Status and in the BLASTER line.
     Music:           General MIDI / Roland Sound Canvas / MPU-401, port
                      330; or "Roland MT-32" when you chose the MT-32; or
                      AdLib / Sound Blaster FM.
  You do NOT load SBEMU, HDPMI or SC55.BAT inside Windows: this program
  takes their place there.

  Windows programs: choose the MIDI device "NUKED SB-EMU" (or leave the
  MIDI Mapper, if Setup made it the default). Control Panel > Multimedia >
  MIDI shows and changes the default.

  Command line (Start > Run):
     C:\SC55\WIN98\SC55W.EXE              start (or show the status)
     C:\SC55\WIN98\SC55W.EXE /SETTINGS    open the settings
     C:\SC55\WIN98\SC55W.EXE /EXIT        stop it


-----------------------------------------------------------------------------
 3. SETTINGS
-----------------------------------------------------------------------------

The same options as SETTINGS.BAT / SBSET, with the SBEMU switch they set:

  Sound Blaster (DOS)
     Port 220/240 (/A), IRQ 5/7/9/10 (/I), 8-bit DMA 0/1/3 (/D), 16-bit
     DMA 5/6/7 (/H, SB16 only), Type 1-6 (/T: SB 1.5, SB 2.0, SB Pro,
     SB Pro 2, SB16), FM music off / DOSBox (fast) / Nuked (accurate)
     (/OPL, /FMCORE).
  MIDI
     MPU-401 330 / 300 / off (/P, /VMPU), synth: Roland SC-55, SoundFont
     (/VSF), MT-32 auto / MT-32 / CM-32L (/VMT32), SC-55 ROM set (/VSCSET,
     "auto" or one of the sets found in C:\SC55\ROMS).
  SC-55 / MT-32
     Gain (/VSCGAIN, 100 = unity), note limit (/VSCVOICES), reverb and
     chorus (/VSCFX), automatic speed control (/VSCAUTO: pauses the SC-55's
     reverb/chorus while the CPU cannot keep up).
  SoundFont
     reverb, room, damping, chorus, chorus speed and depth, voices, follow
     the song's reverb/chorus sends (/VSFREV ... /VSFSEND).
  Volume
     master (/VOL), sound effects (/VSBVOL), FM (/VOPLVOL), MIDI (/VMIDIVOL,
     0-400). The DOS program's own Sound Blaster mixer settings apply too.
  Output
     sample rate (/K: 22050 / 32000 / 44100 / 48000), render-ahead
     (/VSCAHEAD: how far behind the game the music is made; in Windows at
     least 20 ms), fix time constants (/FIXTC).
  Start with Windows.

The line at the bottom shows exactly what is saved to SBOPTS.TXT. Options
that only matter in DOS (/VTIMER, /O, /PM, /RM, /VCDA, /MCOM ...) are kept
unchanged in the file.

  Save     writes SBOPTS.TXT (and SBOPTS.BAT) and applies the settings.
  Apply    applies them without saving (Cancel then goes back).
  Defaults the pack's defaults.

Changes take effect at once - for DOS boxes too (a running game may need to
be restarted to see a new port or IRQ). Volume and gain changes are
instant; changing the synth, ROM set or sample rate restarts the synth (a
few seconds of silence).

Windows-only options, in C:\SC55\WIN98\SC55W.INI under [SC55W]:
     BufferMs=80        sound output buffer (20-500). Raise it if the
                        sound breaks up; lower it for less delay.
     BlockMs=10         size of each buffer block (5-50).
     RecordWav=C:\X.WAV also record everything played to a WAV file
                        (troubleshooting).


-----------------------------------------------------------------------------
 4. CPU LOAD (D810)
-----------------------------------------------------------------------------

The SC-55 emulation is heavy: about as heavy as under DOS. Status shows the
load. If music slows down or breaks up:
  * use 22050 or 32000 Hz (Settings > Output),
  * leave "Automatic speed control" on, or turn reverb and chorus off,
  * set a note limit of 16,
  * or use the original SC-55 ROM set instead of the mkII.
The DOS pack's Automatic setup (SETTINGS /TUNE) finds good values for the
D810; they are saved in SBOPTS.TXT, which Windows uses too.
When nothing has played for a few seconds the synths pause, so the program
uses almost no CPU while idle.


-----------------------------------------------------------------------------
 5. TROUBLESHOOTING
-----------------------------------------------------------------------------

  "Port 220 is taken by another driver"
     Another Sound Blaster emulation has the port: SBEMUL (run Setup again
     with that box ticked), or a sound card driver with its own DOS
     support - turn that off in its settings, or choose port 240 here.
  "IRQ n / DMA n could not be taken over"
     Choose another IRQ (5, 7, 9 or 10) or DMA channel; the game's setup
     must then use the same values.
  "Cannot open the sound output"
     The Windows sound driver is missing or busy. Check that Windows can
     play a sound (Control Panel > Sounds).
  No MIDI in a Windows program
     SC55W.EXE must be running (tray icon). Check Control Panel >
     Multimedia > MIDI, or pick "NUKED SB-EMU" in the program.
  Stuck notes:   tray icon > All notes off.
  Sound breaks up: raise BufferMs (section 3), lower the sample rate.
  C:\SC55\WIN98\SC55W.LOG shows what happened at the last start.


-----------------------------------------------------------------------------
 6. UNINSTALLING
-----------------------------------------------------------------------------

Control Panel > Add/Remove Programs > "NUKED SB-EMU for Windows 98", or
C:\SC55\WIN98\SC55W.EXE /UNINSTALL. It removes the MIDI device, the start
entry and the BLASTER line, turns SBEMUL back on if Setup turned it off,
and restores the previous default MIDI device. The DOS pack, ROMs and your
settings stay. Restart Windows afterwards.

If you also remove the DOS pack (UNINSTAL.BAT), remove this Windows part
first: UNINSTAL.BAT deletes C:\SC55 including C:\SC55\WIN98.


-----------------------------------------------------------------------------
 7. FILES AND CREDITS
-----------------------------------------------------------------------------

  FILES\WIN98\SC55W.EXE     the program (synths, settings, setup)
  FILES\WIN98\SC55W.VXD     the virtual Sound Blaster / OPL3 / MPU-401
                            (a dynamic VxD, loaded by SC55W.EXE)
  FILES\WIN98\SC55MIDI.DRV  the Windows MIDI driver ("NUKED SB-EMU")
  FILES\WIN98\SOURCE.ZIP    source code of the three
  FILES\WIN98\COPYING.TXT   GNU General Public License, version 2

  Built on: Nuked-SC55 (nukeykt, jcmoyer fork; GPL v2+), Munt mt32emu
  (LGPL v2.1+), TinySoundFont (MIT), DOSBox DBOPL (GPL v2+), Nuked OPL3
  (LGPL v2.1+), the SBEMU sources (GPL). SC55W itself is GPL v2 or later.
  Roland ROMs are not included in the source; use your own dumps.
