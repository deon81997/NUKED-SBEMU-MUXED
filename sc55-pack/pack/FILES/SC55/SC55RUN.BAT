@ECHO OFF
REM SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
REM Runs SBEMU with your saved settings and shows its messages. Called by SC55.BAT / SC55JEM.BAT /
REM SC55QMM.BAT - not meant to be run by hand. The settings are passed as a response file
REM (@SBOPTS.TXT), not through the small DOS environment, so they can be as long as needed.
IF "%SBEMULOG%"=="" ECHO Starting SBEMU ...
IF NOT "%SBEMULOG%"=="" ECHO Starting SBEMU - its messages are saved to %SBEMULOG% and shown below ...
REM CD-Audio of mounted disc images: SBEMU links to the virtual CD drive only if uCDD is already in
REM memory when SBEMU starts (a drive turned on later from MENU had silent CD music before beta.115).
REM So uCDD is loaded first (no drive letter yet - CDON.BAT gives M: when a disc is mounted) and CD music
REM is on at 100%; SETTINGS > CD audio volume "Off" writes /VCDA:0 into SBOPTS.TXT, which comes later.
SET SBCDA=
REM SET SBNOCDA=1 skips uCDD and CD music (beta.112 start-up; P5TEST.BAT uses it to bisect a freeze).
IF "%SBNOCDA%"=="1" GOTO RUN
IF NOT EXIST C:\CDSET\UCDD.EXE GOTO RUN
C:\CDSET\UCDD.EXE -install > NUL
SET SBCDA=/VCDA:100
:RUN
IF EXIST SBOPTS.TXT GOTO TXT
IF EXIST SBOPTS.BAT GOTO BAT
SBEMU.EXE /VMPU /VSCROM:ROMS %SBCDA% %1 %2 %3 %4
GOTO SHOW
:TXT
SBEMU.EXE /VMPU /VSCROM:ROMS %SBCDA% @SBOPTS.TXT %1 %2 %3 %4
GOTO SHOW
:BAT
SBEMU.EXE /VMPU /VSCROM:ROMS %SBCDA% @SBOPTS.BAT %1 %2 %3 %4
:SHOW
IF "%SBEMULOG%"=="" GOTO MOUSE
IF EXIST %SBEMULOG% TYPE %SBEMULOG%
:MOUSE
REM not in a Windows DOS box (SBEMU did not load there; Windows has its own mouse)
IF NOT "%windir%"=="" GOTO END
REM the DOS mouse driver goes after SBEMU (for a USB mouse); MOUSE\MOUSE.BAT says which
IF EXIST MOUSE\MOUSE.BAT CALL MOUSE\MOUSE.BAT
:END
SET SBEMULOG=
SET SBCDA=
