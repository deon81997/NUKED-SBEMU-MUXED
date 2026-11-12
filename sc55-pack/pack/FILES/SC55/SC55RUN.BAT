@ECHO OFF
REM SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
REM Runs SBEMU with your saved settings and shows its messages. Called by SC55.BAT / SC55JEM.BAT /
REM SC55QMM.BAT - not meant to be run by hand. The settings are passed as a response file
REM (@SBOPTS.TXT), not through the small DOS environment, so they can be as long as needed.
IF "%SBEMULOG%"=="" ECHO Starting SBEMU ...
IF NOT "%SBEMULOG%"=="" ECHO Starting SBEMU - its messages are saved to %SBEMULOG% and shown below ...
IF EXIST SBOPTS.TXT GOTO TXT
IF EXIST SBOPTS.BAT GOTO BAT
SBEMU.EXE /VMPU /VSCROM:ROMS %1 %2 %3 %4
GOTO SHOW
:TXT
SBEMU.EXE /VMPU /VSCROM:ROMS @SBOPTS.TXT %1 %2 %3 %4
GOTO SHOW
:BAT
SBEMU.EXE /VMPU /VSCROM:ROMS @SBOPTS.BAT %1 %2 %3 %4
:SHOW
IF "%SBEMULOG%"=="" GOTO MOUSE
IF EXIST %SBEMULOG% TYPE %SBEMULOG%
:MOUSE
REM the DOS mouse driver goes after SBEMU (for a USB mouse); MOUSE\MOUSE.BAT says which
IF EXIST MOUSE\MOUSE.BAT CALL MOUSE\MOUSE.BAT
:END
SET SBEMULOG=
