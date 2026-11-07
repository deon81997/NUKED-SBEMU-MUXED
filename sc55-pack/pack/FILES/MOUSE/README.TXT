DOS MOUSE DRIVERS FOR NUKED SB-EMU
==================================

A USB mouse works in DOS through SBEMU: SBEMU passes its movement to a DOS
mouse driver as if it were a PS/2 mouse. The driver has to be loaded AFTER
SBEMU, and SC55.BAT does that for you by calling MOUSE.BAT in this folder.

  CTMOUSE.EXE   CuteMouse 2.1 beta 4, free software (GPL-2.0, see COPYING).
                Supports the mouse wheel. Loaded by default.
                Manual: CTMOUSE.TXT. Full original package with the source
                code: CTM21B4.ZIP.
                https://cutemouse.sourceforge.net/

  MOUSE.COM     Microsoft Mouse Driver 8.20 (not included, owned by
                Microsoft). Put your own copy here and change the GOTO line
                in MOUSE.BAT to GOTO MS to use it instead.

To load no mouse driver, change the GOTO line in MOUSE.BAT to GOTO NONE.
SBEMU's USB mouse setting: MENU > Settings, or /USBMS:0 (off), /USBMS:1
(when a USB mouse is plugged in at start, the default), /USBMS:2 (always).
KMTEST.EXE shows what DOS gets from the keyboard and the mouse.
