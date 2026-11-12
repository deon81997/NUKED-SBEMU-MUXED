@ECHO OFF
REM SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
REM Loads a DOS mouse driver after SBEMU, so a USB mouse works in DOS: SBEMU
REM hands its movement to the driver as a PS/2 mouse. SC55RUN.BAT calls this.
REM The touchpad or a PS/2 mouse works with it too.
REM Which driver: change the GOTO line below.
REM   GOTO CUTE   CuteMouse 2.1b4 (free, included; mouse wheel too)
REM   GOTO MS     Microsoft MOUSE.COM 8.20, if MOUSE.COM is in this folder
REM   GOTO NONE   no mouse driver
GOTO CUTE

:CUTE
IF NOT EXIST MOUSE\CTMOUSE.EXE GOTO MS
MOUSE\CTMOUSE.EXE
GOTO END

:MS
IF NOT EXIST MOUSE\MOUSE.COM GOTO NOMS
MOUSE\MOUSE.COM
GOTO END

:NOMS
ECHO No mouse driver in the MOUSE folder (CTMOUSE.EXE or MOUSE.COM).
GOTO END

:NONE
:END
