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
    MIDI device called "NUKED SB-EMU" played by the same synth. Its volume
    follows Windows (games' music sliders, midiOutSetVolume).
  * DOS programs also get a joystick: a game port (201h, or any address
    200h-20Fh) and the BIOS joystick call (INT 15h AH=84h), fed by the
    joystick Windows knows (an Xbox 360 pad with the driver that comes
    with the pack, or any USB pad). Stick types as in DOS: standard, CH
    Flightstick Pro, Thrustmaster FCS, Gravis GamePad Pro (GrIP).
  * Windows games get the "NUKED SB-EMU Joystick" (Control Panel > Game
    Controllers), the pad as NUKED SB-EMU passes it on, and PCs whose
    sound card has no game port get a "NUKED SB-EMU Game Port" in Device
    Manager (see "Joystick" in section 3).

It is made for the Dell Latitude D810 with the SigmaTel AC'97 WDM driver,
but works with any sound card that has a Windows 98 driver. It needs a
CPU with SSE2: Pentium 4, Pentium M, Celeron D or M, Athlon 64 or newer
(a Pentium II / III or Athlon XP cannot run SC55W.EXE).

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
                        only if AUTOEXEC.BAT has no BLASTER line of its own:
                        an existing line is never changed, because SBEMU
                        and your games' setups go by it. Ours is kept up to
                        date when you change the settings. (Version 1 of
                        Setup replaced an existing line, which could make
                        games freeze; running Setup again puts it back.)
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

  Joystick in DOS games: set it up in Windows first (Control Panel > Game
  Controllers: add it, calibrate it, check it on the Test page), then pick
  "Joystick" / "Gravis" / "CH" in the game's setup as usual.
     Stick 1     = Windows joystick 1, X and Y
     Stick 2     = Windows joystick 2, or joystick 1's second stick
                   (U or Z across, R down) when there is no joystick 2
     Buttons 1-4 = joystick 1's buttons 1-4 (joystick 2's buttons 1-2
                   also press buttons 3-4)
  Status shows the joystick Windows found and how often DOS read it.
  Windows' own programs keep using the joystick through Windows as before;
  they can also use the NUKED SB-EMU Joystick (Settings > Joystick).

  Xbox 360 controller: Windows 98 has no driver for it, so the pack has an
  unofficial one in WIN98\X360 (copied there by Setup; FILES\WIN98\X360 in
  the pack). Plug the pad in and point the Add New Hardware Wizard to that
  folder - X360.TXT has the steps. Then the left stick is DOS stick 1, the
  right stick DOS stick 2, and A B X Y buttons 1-4. When a 360 pad is
  plugged in without a driver, NUKED SB-EMU says so once and offers to show
  the steps (SC55W.INI X360Prompt=0 stops that).

  Game pad buttons as keys: when a DOS box has the keyboard, the pad's
  buttons and sticks can press keys, with a layout for the game running in
  it (found by its EXE name and size, or by the files in its folder).
     Layouts for 3500+ games: download keyb2joypad.cpp as DOWNLOADS.TXT
       says (the Keyb2Joypad project, as built into DOSBox Pure; not
       shipped with the pack) into C:\SC55\PADMAP (Setup copies it from
       the pack's FILES\PADMAP). The next start turns it into
       C:\SC55\PADMAP.TXT.
     Without a layout for the game ("Default"): sticks and A B X Y are the
       DOS joystick as above, D-pad = arrow keys, Start = Enter, Back = Esc.
     Your own layouts and changes: C:\SC55\PADUSER.TXT, same format as
       PADMAP.TXT. A section with a game's title (or [Default]) changes only
       the lines it has, e.g.
          [Default]
          Y=F1
          [Commander Keen 4 - Secret of the Oracle]
          RB=F2+F3
       Buttons: A B X Y LB RB LT RT LS RS Back Start DUp DDown DLeft DRight
       Sticks:  LStickX LStickY RStickX RStickY = key-for-left/up key-for-
       right/down. Keys as in PADMAP.TXT (LCtrl, Space, Up, F1, A ... Z,
       None); JoyButton1-4, JoyUp/Down/Left/Right, Joy2Up... = DOS joystick.
     Status shows the layout in use and the last button pressed with its
     Windows button number, to check the [Pad] settings below.
     Held keys are let go when Windows takes the keyboard.

  Command line (Start > Run):
     C:\SC55\WIN98\SC55W.EXE              start (or show the status)
     C:\SC55\WIN98\SC55W.EXE /SETTINGS    open the settings
     C:\SC55\WIN98\SC55W.EXE /EXIT        stop it


-----------------------------------------------------------------------------
 3. SETTINGS
-----------------------------------------------------------------------------

Three tabs: "Sound and MIDI", "Joystick" and "Pad keys" (Ctrl+Tab goes to
the next one). Save / Apply / Cancel are for all three.

"Sound and MIDI" has the same options as SETTINGS.BAT / SBSET, with the
SBEMU switch they set:

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

"Joystick":
  Game port for DOS programs
     Game port in DOS boxes   on/off (off = a real game port card answers
                              DOS directly)
     Address                  200-20F (201 is what nearly every game uses;
                              a PC with a real game port on 201 can put
                              ours elsewhere). DOS boxes use it at once.
     Stick type               Standard (2 sticks, 4 buttons), CH
                              Flightstick Pro (D-pad = the hat), Thrustmaster
                              FCS (D-pad = the hat on axis 4), Gravis GamePad
                              Pro digital (GrIP: all buttons of 2 pads) - as
                              SBEMU's /JOYMODE in DOS. Pick the same in the
                              game's setup.
     BIOS joystick call       INT 15h AH=84h for the games that use it.
     Also for Windows programs that read the port
                              old Windows programs that read the game port
                              themselves (not through Windows) get ours too.
                              Leave it off if the PC has a real game port.
  NUKED joystick for Windows games
     Feed the NUKED SB-EMU Joystick
                              Windows joystick "NUKED SB-EMU Joystick" shows
                              the pad: left stick X/Y, triggers Z, right
                              stick R (down) / U (across), D-pad = the hat,
                              buttons 1-12 (A B X Y LB RB Back Start LS RS
                              LT RT) - or, with "It shows: the game port as
                              DOS sees it", the 4 axes and 4 buttons of the
                              game port above (with its stick type).
     Dead zone                % of the stick's travel around the middle
                              that counts as centred (0-50, default 10).
     Set up...                adds the "NUKED SB-EMU Game Port" to Device
                              Manager (Sound, video and game controllers,
                              at the Address above) and the "NUKED SB-EMU
                              Joystick" to Game Controllers, then asks for a
                              restart. No Windows CD needed. Changing the
                              Address later moves the Device Manager entry
                              too (shown after a restart).
     Remove                   takes both out again (Uninstall does too).
     Game Controllers...      Control Panel > Game Controllers (calibrate,
                              test, the Xbox 360 driver's own settings).
  Test (live)                 the pad as Windows gives it (sticks, buttons),
                              what DOS boxes read from the game port, and
                              what the NUKED joystick shows Windows games.
  The NUKED joystick only moves while SC55W runs; Windows games that want
  joystick 1 use whatever Game Controllers lists first (Advanced tab:
  "Preferred device").

"Pad keys":
  Pad buttons press keys in DOS games    on/off (as PadKeys= below)
  Layout now / Last pad button           what is in use right now
  My layouts (PADUSER.TXT)...            opens it in Notepad
  PADMAP folder...                       where keyb2joypad.cpp goes
  Which Windows joystick button is which pad button: the [Pad] settings
  below. Click a box, press the button on the pad and its number is filled
  in (then the next box gets the cursor). "Xbox 360 defaults" puts the
  360 driver's numbers back.

Windows-only options, in C:\SC55\WIN98\SC55W.INI under [SC55W]:
     BufferMs=80        sound output buffer (20-500). Raise it if the
                        sound breaks up; lower it for less delay.
     BlockMs=10         size of each buffer block (5-50).
     OutputSleep=1      stop the sound output while nothing makes a sound
                        (saves CPU; it starts again within about 20 ms).
                        0 = keep it running, if your sound card's driver
                        clicks or misses the first note after a pause.
     RecordWav=C:\X.WAV also record everything played to a WAV file
                        (troubleshooting).
     Joystick=1         game port for DOS programs (0 = off, e.g. for a
                        real game port card that DOS should read directly).
     JoyBios=1          BIOS joystick call INT 15h AH=84h (0 = off).
     X360Prompt=1       0 = do not offer the Xbox 360 driver when a 360 pad
                        without a driver is plugged in.
     PadKeys=1          game pad buttons -> keys in DOS boxes (0 = off).
   Under [Pad]: which Windows joystick button is which pad button, for pads
   that number them differently from the Xbox 360 driver (its order is the
   default: A=1 B=2 X=3 Y=4 LB=5 RB=6 Back=7 Start=8 LS=9 RS=10; D-pad =
   the POV hat; Triggers=Z (LT one way, RT the other); RStickX=U RStickY=R).
     CpuMax=60          most of the CPU (percent, 20-95) the synth may use.
                        When it cannot keep up the music lags or breaks up
                        instead of Windows slowing down.
     StartDelay=15      when it starts with Windows: seconds to wait before
                        starting the synth, so the other startup programs
                        get the CPU first (0 = start at once).
   Under [Joystick] (the Joystick tab writes them):
     Port=201           game port address, 200-20F
     Type=0             stick type: 0 standard, 1 CH, 2 FCS, 3 GrIP
     WindowsPort=0      1 = the game port for Windows programs too
     NukedJoystick=1    feed the NUKED SB-EMU Joystick
     NukedMode=0        0 = the pad, 1 = the game port as DOS sees it
     DeadZone=10        percent

If Windows froze while NUKED SB-EMU was starting, the next start with Windows
is skipped and a message tells you at which step it stopped (in
SC55W.INI [State] Step=). Start C:\SC55\WIN98\SC55W.EXE by hand, and if
Windows freezes again, send C:\SC55\WIN98\SC55W.LOG: for its first minute
it writes the load and priority every 5 seconds.


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
When nothing has played for a few seconds the synths pause, and a second
later the sound output stops too (Status: "paused while silent"), so the
program uses almost no CPU while idle: on a Pentium II 450 about 0.25 % of
the CPU (it was 1.6 % before beta.81-w98.4). Windows' timer runs at its
normal rate unless a DOS program plays sound or reads the joystick.


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
restores the previous default MIDI device and takes out the NUKED SB-EMU
Game Port and Joystick if they were set up. The DOS pack, ROMs and your
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
  FILES\WIN98\JOY\NUKEDJOY.VXD
                            the NUKED SB-EMU Joystick (a VJoyD joystick
                            minidriver: Windows loads it for that joystick)
  FILES\WIN98\JOY\NUKEDJOY.INF
                            the NUKED SB-EMU Game Port, for Add New Hardware
                            by hand (Settings > Joystick > Set up... does
                            the same without it)
  FILES\WIN98\SOURCE.ZIP    source code of all of them
  FILES\WIN98\COPYING.TXT   GNU General Public License, version 2

  Built on: Nuked-SC55 (nukeykt, jcmoyer fork; GPL v2+), Munt mt32emu
  (LGPL v2.1+), TinySoundFont (MIT), DOSBox DBOPL (GPL v2+), Nuked OPL3
  (LGPL v2.1+), the SBEMU sources (GPL). SC55W itself is GPL v2 or later.
  Roland ROMs are not included in the source; use your own dumps.
