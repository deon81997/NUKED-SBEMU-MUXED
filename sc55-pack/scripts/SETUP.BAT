@ECHO OFF
REM ---------------------------------------------------------------
REM  SETUP.BAT - prepares the folders for SBEMU + SC-55 emulation
REM  Run it from the folder you unpacked the files into.
REM  Usage:  SETUP [drive letter] [AUTO]      (default drive is C)
REM           AUTO also adds the SC-55 to AUTOEXEC.BAT so it starts at boot
REM ---------------------------------------------------------------
SET DRV=%1
IF "%DRV%"=="" SET DRV=C
SET DST=%DRV%:\SC55
SET AUT=%2

IF NOT EXIST SBEMU.EXE GOTO NOFILES
IF NOT EXIST SC55LOG.EXE GOTO NOFILES
IF NOT EXIST SC55WAV.EXE GOTO NOFILES

ECHO Setting up %DST% ...
IF NOT EXIST %DRV%:\NUL GOTO NODRIVE

IF NOT EXIST %DST%\NUL MD %DST%
IF NOT EXIST %DST%\NUL GOTO MDFAIL
IF NOT EXIST %DST%\ROMS\NUL MD %DST%\ROMS
IF NOT EXIST %DRV%:\HDPMI\NUL MD %DRV%:\HDPMI
IF NOT EXIST %DRV%:\JEMM\NUL MD %DRV%:\JEMM

ECHO Copying programs ...
COPY SBEMU.EXE %DST% > NUL
COPY SC55WAV.EXE %DST% > NUL
COPY SC55LOG.EXE %DST% > NUL
IF EXIST MPUTEST.EXE COPY MPUTEST.EXE %DST% > NUL
IF NOT EXIST %DST%\SBEMU.EXE GOTO COPYFAIL
IF NOT EXIST %DST%\SC55LOG.EXE GOTO COPYFAIL
IF NOT EXIST %DST%\SC55WAV.EXE GOTO COPYFAIL
IF EXIST README-VSC55.MD COPY README-VSC55.MD %DST%\README.MD > NUL

ECHO Creating start-up files ...
ECHO @ECHO OFF> %DST%\SC55.BAT
ECHO REM Starts the SC-55 MPU-401 at port 330h. Extra options can follow, e.g. SC55 /FMCORE:1>> %DST%\SC55.BAT
ECHO %DRV%:>> %DST%\SC55.BAT
ECHO CD \SC55>> %DST%\SC55.BAT
ECHO IF NOT EXIST ROMS\NUL GOTO NOROMS>> %DST%\SC55.BAT
ECHO IF EXIST %DRV%:\HDPMI\HDPMI32I.EXE %DRV%:\HDPMI\HDPMI32I.EXE -r>> %DST%\SC55.BAT
ECHO IF NOT EXIST %DRV%:\HDPMI\HDPMI32I.EXE HDPMI32I -r>> %DST%\SC55.BAT
ECHO SBEMU.EXE /VMPU /VSCROM:ROMS %%1 %%2 %%3 %%4>> %DST%\SC55.BAT
ECHO GOTO END>> %DST%\SC55.BAT
ECHO :NOROMS>> %DST%\SC55.BAT
ECHO ECHO ROMS folder not found. Run SETUP again.>> %DST%\SC55.BAT
ECHO :END>> %DST%\SC55.BAT

ECHO @ECHO OFF> %DST%\TEST.BAT
ECHO REM Checks that your ROMs boot and measures speed. Writes SC55LOG.TXT in this folder.>> %DST%\TEST.BAT
ECHO %DRV%:>> %DST%\TEST.BAT
ECHO CD \SC55>> %DST%\TEST.BAT
ECHO SC55LOG.EXE ROMS %%1 %%2 %%3>> %DST%\TEST.BAT
ECHO ECHO Report saved in %DST%\SC55LOG.TXT>> %DST%\TEST.BAT

ECHO Put your SC-55 ROM files in this folder (any file names).> %DST%\ROMS\README.TXT
ECHO They are recognised by their contents, not by name. No ROMs are included.>> %DST%\ROMS\README.TXT

ECHO.
ECHO Done. Folders:
ECHO   %DST%          programs, SC55.BAT (start) and TEST.BAT (benchmark)
ECHO   %DST%\ROMS     copy your SC-55 ROM files here
ECHO   %DRV%:\HDPMI   copy HDPMI32I.EXE here
ECHO   %DRV%:\JEMM    copy JEMMEX.EXE, JLOAD.EXE, QPIEMU.DLL here (real-mode games)
ECHO.
ECHO Next: copy the ROMs, run %DST%\TEST.BAT, then %DST%\SC55.BAT before your game.
ECHO Then %DST%\MPUTEST.EXE plays a test tune through the running SC-55.
IF "%AUT%"=="AUTO" GOTO AUTOEXEC
IF "%AUT%"=="auto" GOTO AUTOEXEC
ECHO To start the SC-55 at every boot, run:  SETUP %DRV% AUTO
GOTO END

:AUTOEXEC
ECHO.
IF NOT EXIST %DRV%:\AUTOEXEC.BAT GOTO NEWAE
FIND /I "SC55.BAT" %DRV%:\AUTOEXEC.BAT > NUL
IF NOT ERRORLEVEL 1 GOTO ALREADY
IF NOT EXIST %DRV%:\AUTOEXEC.SC5 COPY %DRV%:\AUTOEXEC.BAT %DRV%:\AUTOEXEC.SC5 > NUL
ECHO.>> %DRV%:\AUTOEXEC.BAT
:NEWAE
ECHO REM SC-55 MPU-401 emulation (added by SETUP)>> %DRV%:\AUTOEXEC.BAT
IF EXIST %DRV%:\JEMM\JLOAD.EXE ECHO %DRV%:\JEMM\JLOAD.EXE %DRV%:\JEMM\QPIEMU.DLL>> %DRV%:\AUTOEXEC.BAT
ECHO CALL %DST%\SC55.BAT>> %DRV%:\AUTOEXEC.BAT
ECHO Added "CALL %DST%\SC55.BAT" to %DRV%:\AUTOEXEC.BAT.
ECHO Your old file is saved as %DRV%:\AUTOEXEC.SC5. Remove the last two lines to undo.
ECHO Needs HDPMI32I.EXE in %DRV%:\HDPMI. For real-mode games also JEMMEX in CONFIG.SYS and QPIEMU.DLL in %DRV%:\JEMM.
GOTO END
:ALREADY
ECHO AUTOEXEC.BAT already starts the SC-55. Nothing changed.
GOTO END

:NOFILES
ECHO Run SETUP from the folder containing SBEMU.EXE, SC55WAV.EXE and SC55LOG.EXE.
GOTO END
:NODRIVE
ECHO Drive %DRV%: does not exist.
GOTO END
:MDFAIL
ECHO Could not create %DST%. Is the drive writable?
GOTO END
:COPYFAIL
ECHO Copy to %DST% failed. Is there free space on %DRV%: ?
:END
SET DRV=
SET DST=
SET AUT=
