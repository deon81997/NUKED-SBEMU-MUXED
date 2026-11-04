@ECHO OFF
REM ======================================================================
REM  UNINSTAL.BAT - removes everything the SC-55 pack installed and puts the PC back the way it was.
REM
REM    UNINSTAL [drive]       drive = the one you gave SETUP / CDINST (default C)
REM
REM  1. CONFIG.SYS / AUTOEXEC.BAT: if BOOTCFG.BAT or JEMMCFG.BAT changed them, they are put back from
REM     the backups those made (the same as C:\RESET.BAT). If that fails, NOTHING else is removed.
REM  2. Asks whether to keep your own files: ROM dumps and SoundFonts (moved to C:\SC55SAVE), settings
REM     (SBOPTS.TXT, THEME.CFG, GAMES.CFG ..., copied there too) and disc images (left in C:\CDIMAGES). Or delete them as well.
REM  3. Removes   C:\SC55   C:\CDSET   C:\MOUNT.BAT   C:\RESET.BAT
REM               C:\HDPMI\HDPMI32I.EXE  and the LG driver files in C:\GSCDROM  (each folder only if
REM               nothing else is left in it)   the test disc in C:\CDIMAGES
REM               leftovers of old versions of the pack (the list CLEANUP.BAT uses)
REM  It never deletes CONFIG.SYS, AUTOEXEC.BAT or backups it did not make. The pack folder you
REM  extracted (NUKEDSB) is not touched: delete it yourself when you no longer need it.
REM  Best run right after a restart, before SC55 or the CD tools are started.
REM ======================================================================
SET DRV=%1
IF "%DRV%"=="" SET DRV=C
IF NOT EXIST %DRV%:\NUL GOTO NODRIVE
SET UN_KEEP=

ECHO.
ECHO  NUKED SB-EMU - UNINSTALL
ECHO  ======================
ECHO.
ECHO  This removes (whatever of it exists):
IF EXIST %DRV%:\SC55\NUL ECHO    folder  %DRV%:\SC55        SBEMU, SC-55 tools, settings, logs, CD-ROM drivers
IF EXIST %DRV%:\CDSET\NUL ECHO    folder  %DRV%:\CDSET       virtual CD drive and disc menu
IF EXIST %DRV%:\CDIMAGES\TEST.CUE ECHO    files   %DRV%:\CDIMAGES\TEST.CUE / TEST.BIN   (the pack's test disc)
IF EXIST %DRV%:\HDPMI\HDPMI32I.EXE ECHO    file    %DRV%:\HDPMI\HDPMI32I.EXE
IF EXIST %DRV%:\GSCDROM\GSCDROM.SYS ECHO    files   %DRV%:\GSCDROM\ (LG CD-ROM driver)
IF EXIST %DRV%:\MOUNT.BAT ECHO    file    %DRV%:\MOUNT.BAT
IF EXIST C:\RESET.BAT ECHO    file    C:\RESET.BAT
IF EXIST C:\JEMMEX\NUL ECHO    folder  C:\JEMMEX   (old pack)
IF EXIST C:\UCDD\NUL ECHO    folder  C:\UCDD   (old pack)
IF EXIST C:\OAKCD\NUL ECHO    folder  C:\OAKCD   (old pack)
IF EXIST C:\XCDROM\NUL ECHO    folder  C:\XCDROM   (old pack)
ECHO.
IF EXIST C:\SC55BOOT.FLG ECHO  CONFIG.SYS and AUTOEXEC.BAT were changed by BOOTCFG/JEMMCFG: they are put back first.
IF NOT EXIST C:\SC55BOOT.FLG ECHO  CONFIG.SYS and AUTOEXEC.BAT: not changed by this pack, left as they are.
FOR %%F IN (SC5 XCD OAK SHS) DO IF EXIST C:\CONFIG.%%F GOTO OLDBK
FOR %%F IN (SC5 XCD OAK SHS) DO IF EXIST C:\AUTOEXEC.%%F GOTO OLDBK
IF NOT EXIST C:\SC55BOOT.FLG IF EXIST C:\CONFIG.PRE GOTO OLDBK
IF NOT EXIST C:\SC55BOOT.FLG IF EXIST C:\AUTOEXEC.PRE GOTO OLDBK
GOTO ASK
:OLDBK
ECHO.
ECHO  Backups from OLDER versions of the pack were found (not touched - they may be your originals):
IF EXIST C:\CONFIG.SC5 ECHO    C:\CONFIG.SC5
IF EXIST C:\AUTOEXEC.SC5 ECHO    C:\AUTOEXEC.SC5
IF EXIST C:\CONFIG.XCD ECHO    C:\CONFIG.XCD
IF EXIST C:\AUTOEXEC.XCD ECHO    C:\AUTOEXEC.XCD
IF EXIST C:\CONFIG.OAK ECHO    C:\CONFIG.OAK
IF EXIST C:\AUTOEXEC.OAK ECHO    C:\AUTOEXEC.OAK
IF EXIST C:\CONFIG.SHS ECHO    C:\CONFIG.SHS
IF EXIST C:\AUTOEXEC.SHS ECHO    C:\AUTOEXEC.SHS
IF NOT EXIST C:\SC55BOOT.FLG IF EXIST C:\CONFIG.PRE ECHO    C:\CONFIG.PRE
IF NOT EXIST C:\SC55BOOT.FLG IF EXIST C:\AUTOEXEC.PRE ECHO    C:\AUTOEXEC.PRE
ECHO  If your CONFIG.SYS / AUTOEXEC.BAT still load old pack drivers, compare them with these
ECHO  (EDIT C:\CONFIG.SYS) and copy one back yourself, e.g.  COPY C:\AUTOEXEC.SC5 C:\AUTOEXEC.BAT

:ASK
ECHO.
CHOICE /C:YN /N  Uninstall now (Y/N)?
IF ERRORLEVEL 2 GOTO CANCEL
IF ERRORLEVEL 1 GOTO ASK2
ECHO CHOICE.COM is not available (it is in C:\WINDOWS\COMMAND). Nothing was changed.
GOTO END

:ASK2
ECHO.
ECHO  Your own files: SC-55 ROM dumps (%DRV%:\SC55\ROMS), SoundFonts (%DRV%:\SC55\SF2),
ECHO  your settings (SBOPTS.TXT, style, games list) and disc images (%DRV%:\CDIMAGES).
ECHO    K = keep them (ROMs, SoundFonts and settings go to %DRV%:\SC55SAVE, images stay)
ECHO    D = delete them too (back to exactly how the PC was before the install)
CHOICE /C:KD /N  Keep or Delete (K/D)?
IF ERRORLEVEL 2 GOTO BOOT
SET UN_KEEP=1

REM ---------------------------------------------------------------- 1. boot files
:BOOT
ECHO.
IF NOT EXIST C:\SC55BOOT.FLG GOTO SAVE
ECHO Restoring CONFIG.SYS and AUTOEXEC.BAT ...
IF EXIST C:\CONFIG.SYS ATTRIB -R -H -S C:\CONFIG.SYS
IF EXIST C:\AUTOEXEC.BAT ATTRIB -R -H -S C:\AUTOEXEC.BAT
IF EXIST C:\CONFIG.PRE TYPE C:\CONFIG.PRE > C:\CONFIG.SYS
IF EXIST C:\CONFIG.NON DEL C:\CONFIG.SYS
IF EXIST C:\AUTOEXEC.PRE TYPE C:\AUTOEXEC.PRE > C:\AUTOEXEC.BAT
IF EXIST C:\AUTOEXEC.NON DEL C:\AUTOEXEC.BAT
IF NOT EXIST C:\CONFIG.PRE GOTO BOOTCHK2
FC C:\CONFIG.PRE C:\CONFIG.SYS > NUL
IF ERRORLEVEL 1 GOTO BOOTFAIL
:BOOTCHK2
IF NOT EXIST C:\AUTOEXEC.PRE GOTO BOOTOK
FC C:\AUTOEXEC.PRE C:\AUTOEXEC.BAT > NUL
IF ERRORLEVEL 1 GOTO BOOTFAIL
:BOOTOK
IF EXIST C:\CONFIG.PRE DEL C:\CONFIG.PRE
IF EXIST C:\CONFIG.NON DEL C:\CONFIG.NON
IF EXIST C:\AUTOEXEC.PRE DEL C:\AUTOEXEC.PRE
IF EXIST C:\AUTOEXEC.NON DEL C:\AUTOEXEC.NON
IF EXIST C:\SC55BOOT.FLG DEL C:\SC55BOOT.FLG
ECHO   done - both files are back to what they were before BOOTCFG/JEMMCFG.

REM ---------------------------------------------------------------- 2. keep the user's files
:SAVE
IF NOT "%UN_KEEP%"=="1" GOTO REMOVE
IF NOT EXIST %DRV%:\SC55\NUL GOTO REMOVE
ECHO Moving your ROMs, SoundFonts and settings to %DRV%:\SC55SAVE ...
IF NOT EXIST %DRV%:\SC55SAVE\NUL MD %DRV%:\SC55SAVE
IF NOT EXIST %DRV%:\SC55SAVE\NUL GOTO SAVEFAIL
IF NOT EXIST %DRV%:\SC55\ROMS\*.* GOTO SAVESF
IF NOT EXIST %DRV%:\SC55SAVE\ROMS\NUL MD %DRV%:\SC55SAVE\ROMS
MOVE /Y %DRV%:\SC55\ROMS\*.* %DRV%:\SC55SAVE\ROMS > NUL
IF EXIST %DRV%:\SC55\ROMS\*.* GOTO SAVEFAIL
:SAVESF
IF NOT EXIST %DRV%:\SC55\SF2\*.* GOTO SAVEOPT
IF NOT EXIST %DRV%:\SC55SAVE\SF2\NUL MD %DRV%:\SC55SAVE\SF2
MOVE /Y %DRV%:\SC55\SF2\*.* %DRV%:\SC55SAVE\SF2 > NUL
IF EXIST %DRV%:\SC55\SF2\*.* GOTO SAVEFAIL
:SAVEOPT
FOR %%F IN (SBOPTS.TXT SBOPTS.BAT THEME.CFG GAMES.CFG) DO IF EXIST %DRV%:\SC55\%%F COPY %DRV%:\SC55\%%F %DRV%:\SC55SAVE > NUL
FOR %%F IN (GAMETEST.CFG GTBEST.CFG MIDPLAY.CFG MENU.CFG) DO IF EXIST %DRV%:\SC55\%%F COPY %DRV%:\SC55\%%F %DRV%:\SC55SAVE > NUL
ECHO   done. (After a new SETUP, copy them back: ROMS to %DRV%:\SC55\ROMS, SF2 to %DRV%:\SC55\SF2,
ECHO   the .TXT and .CFG files to %DRV%:\SC55.)

REM ---------------------------------------------------------------- 3. remove
:REMOVE
ECHO Removing the pack ...
%DRV%:
CD \
IF EXIST %DRV%:\CDSET\NUL DELTREE /Y %DRV%:\CDSET > NUL
IF EXIST %DRV%:\MOUNT.BAT DEL %DRV%:\MOUNT.BAT
IF EXIST %DRV%:\CDIMAGES\TEST.CUE DEL %DRV%:\CDIMAGES\TEST.CUE
IF EXIST %DRV%:\CDIMAGES\TEST.BIN DEL %DRV%:\CDIMAGES\TEST.BIN
IF "%UN_KEEP%"=="1" GOTO IMGKEPT
IF EXIST %DRV%:\CDIMAGES\NUL DELTREE /Y %DRV%:\CDIMAGES > NUL
:IMGKEPT
IF NOT EXIST %DRV%:\CDIMAGES\NUL GOTO IMGDONE
IF NOT EXIST %DRV%:\CDIMAGES\*.* RD %DRV%:\CDIMAGES
:IMGDONE
FOR %%F IN (CLOSE.EXE EJECT.EXE GSCDROM.SYS) DO IF EXIST %DRV%:\GSCDROM\%%F DEL %DRV%:\GSCDROM\%%F
FOR %%F IN (LGREADME.TXT LOCK.EXE UNLOCK.EXE) DO IF EXIST %DRV%:\GSCDROM\%%F DEL %DRV%:\GSCDROM\%%F
IF NOT EXIST %DRV%:\GSCDROM\NUL GOTO GSDONE
IF NOT EXIST %DRV%:\GSCDROM\*.* RD %DRV%:\GSCDROM
:GSDONE
IF EXIST %DRV%:\HDPMI\HDPMI32I.EXE DEL %DRV%:\HDPMI\HDPMI32I.EXE
IF NOT EXIST %DRV%:\HDPMI\NUL GOTO HDDONE
IF NOT EXIST %DRV%:\HDPMI\*.* RD %DRV%:\HDPMI
:HDDONE
REM leftovers of old pack versions (same list as CLEANUP.BAT)
IF EXIST C:\JEMMEX\NUL DELTREE /Y C:\JEMMEX > NUL
IF EXIST C:\UCDD\NUL DELTREE /Y C:\UCDD > NUL
IF EXIST C:\OAKCD\NUL DELTREE /Y C:\OAKCD > NUL
IF EXIST C:\XCDROM\NUL DELTREE /Y C:\XCDROM > NUL
FOR %%F IN (CONFIG.NEW AUTOEXEC.NEW AUTOEXEC.TMP) DO IF EXIST C:\%%F DEL C:\%%F
FOR %%F IN (CONFIG.NO AUTOEXEC.NO BCDBG.BAT) DO IF EXIST C:\%%F DEL C:\%%F
IF EXIST C:\RESET.BAT DEL C:\RESET.BAT

REM %DRV%:\SC55 last: this file may be running from it, so a tiny helper in the root does it
REM (it deletes itself at the end; a "Batch file missing" line after that is normal).
IF NOT EXIST %DRV%:\SC55\NUL GOTO FINISH
ECHO @ECHO OFF> %DRV%:\SC55UN.BAT
ECHO %DRV%:>> %DRV%:\SC55UN.BAT
ECHO CD \>> %DRV%:\SC55UN.BAT
ECHO DELTREE /Y %DRV%:\SC55>> %DRV%:\SC55UN.BAT
ECHO IF EXIST %DRV%:\SC55\NUL ECHO Could not remove %DRV%:\SC55 - restart the PC and delete it with DELTREE %DRV%:\SC55>> %DRV%:\SC55UN.BAT
ECHO IF NOT EXIST %DRV%:\SC55\NUL ECHO Removed %DRV%:\SC55.>> %DRV%:\SC55UN.BAT
ECHO ECHO.>> %DRV%:\SC55UN.BAT
ECHO ECHO Uninstall finished. Restart the PC.>> %DRV%:\SC55UN.BAT
IF "%UN_KEEP%"=="1" ECHO ECHO Your ROMs, SoundFonts and settings are in %DRV%:\SC55SAVE.>> %DRV%:\SC55UN.BAT
ECHO SET DRV=>> %DRV%:\SC55UN.BAT
ECHO SET UN_KEEP=>> %DRV%:\SC55UN.BAT
ECHO DEL %DRV%:\SC55UN.BAT>> %DRV%:\SC55UN.BAT
%DRV%:\SC55UN.BAT

:FINISH
ECHO.
ECHO Uninstall finished. Restart the PC.
IF "%UN_KEEP%"=="1" ECHO Your ROMs, SoundFonts and settings are in %DRV%:\SC55SAVE.
GOTO END

:BOOTFAIL
ECHO.
ECHO The restored CONFIG.SYS / AUTOEXEC.BAT do not match the backups. NOTHING else was removed,
ECHO and the backups were kept (C:\CONFIG.PRE, C:\AUTOEXEC.PRE). Copy them back yourself:
ECHO    COPY C:\CONFIG.PRE C:\CONFIG.SYS      COPY C:\AUTOEXEC.PRE C:\AUTOEXEC.BAT
ECHO then run UNINSTAL again.
GOTO END
:SAVEFAIL
ECHO.
ECHO Could not move your files to %DRV%:\SC55SAVE (disk full or write-protected?).
ECHO NOTHING was removed, so your ROMs and SoundFonts are safe. (Boot files were already restored.)
GOTO END
:NODRIVE
ECHO Drive %DRV%: does not exist.
GOTO END
:CANCEL
ECHO Nothing was changed.
:END
SET DRV=
SET UN_KEEP=
