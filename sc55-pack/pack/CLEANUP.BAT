@ECHO OFF
REM ======================================================================
REM  CLEANUP.BAT - removes leftovers of OLD installs of this pack. Run it from the pack folder or from C:\SC55.
REM  It shows what it found, asks once, then deletes only these (all from earlier versions of the pack):
REM    folders  C:\JEMMEX  C:\UCDD  C:\OAKCD  C:\XCDROM   (old pack layouts)
REM    files    C:\CDSET\SHSUCDHD.EXE SHCDX86.COM XCDROM.SYS
REM             C:\CONFIG.NEW C:\AUTOEXEC.NEW C:\AUTOEXEC.TMP C:\CONFIG.NO C:\AUTOEXEC.NO C:\BCDBG.BAT
REM  It never edits or deletes CONFIG.SYS, AUTOEXEC.BAT or any backup of them. It only lists the lines
REM  and backups it finds so you can decide (C:\RESET.BAT undoes BOOTCFG.BAT).
REM  Your settings (C:\SC55\SBOPTS.TXT and the .CFG files), ROMs and disc images are never touched.
REM ======================================================================
SET CL_N=0
ECHO.
ECHO Looking for leftovers of old installs ...
ECHO.
IF EXIST C:\JEMMEX\NUL ECHO   folder  C:\JEMMEX   (old memory manager, no longer used)
IF EXIST C:\UCDD\NUL ECHO   folder  C:\UCDD   (old uCDD with the sound code)
IF EXIST C:\OAKCD\NUL ECHO   folder  C:\OAKCD   (old Oak driver scripts)
IF EXIST C:\XCDROM\NUL ECHO   folder  C:\XCDROM   (old XCDROM copy)
IF EXIST C:\CDSET\SHSUCDHD.EXE ECHO   file    C:\CDSET\SHSUCDHD.EXE
IF EXIST C:\CDSET\SHCDX86.COM ECHO   file    C:\CDSET\SHCDX86.COM
IF EXIST C:\CDSET\XCDROM.SYS ECHO   file    C:\CDSET\XCDROM.SYS
IF EXIST C:\CONFIG.NEW ECHO   file    C:\CONFIG.NEW
IF EXIST C:\AUTOEXEC.NEW ECHO   file    C:\AUTOEXEC.NEW
IF EXIST C:\AUTOEXEC.TMP ECHO   file    C:\AUTOEXEC.TMP
IF EXIST C:\CONFIG.NO ECHO   file    C:\CONFIG.NO
IF EXIST C:\AUTOEXEC.NO ECHO   file    C:\AUTOEXEC.NO
IF EXIST C:\BCDBG.BAT ECHO   file    C:\BCDBG.BAT
ECHO.
ECHO Boot-file backups from older installs (NOT deleted, they may be your only originals):
IF EXIST C:\CONFIG.PRE ECHO   C:\CONFIG.PRE
IF EXIST C:\CONFIG.XCD ECHO   C:\CONFIG.XCD
IF EXIST C:\CONFIG.OAK ECHO   C:\CONFIG.OAK
IF EXIST C:\CONFIG.SHS ECHO   C:\CONFIG.SHS
IF EXIST C:\CONFIG.SC5 ECHO   C:\CONFIG.SC5
IF EXIST C:\AUTOEXEC.PRE ECHO   C:\AUTOEXEC.PRE
IF EXIST C:\AUTOEXEC.XCD ECHO   C:\AUTOEXEC.XCD
IF EXIST C:\AUTOEXEC.OAK ECHO   C:\AUTOEXEC.OAK
IF EXIST C:\AUTOEXEC.SHS ECHO   C:\AUTOEXEC.SHS
IF EXIST C:\AUTOEXEC.SC5 ECHO   C:\AUTOEXEC.SC5
ECHO.
ECHO Lines in your boot files that mention old CD tools (not changed):
IF EXIST C:\CONFIG.SYS FIND /I "JEMMEX" C:\CONFIG.SYS
IF EXIST C:\CONFIG.SYS FIND /I "XCDROM" C:\CONFIG.SYS
IF EXIST C:\CONFIG.SYS FIND /I "OAKCD" C:\CONFIG.SYS
IF EXIST C:\AUTOEXEC.BAT FIND /I "MSCDEX" C:\AUTOEXEC.BAT
IF EXIST C:\AUTOEXEC.BAT FIND /I "SHSUCDX" C:\AUTOEXEC.BAT
IF EXIST C:\AUTOEXEC.BAT FIND /I "UCDD" C:\AUTOEXEC.BAT
IF EXIST C:\AUTOEXEC.BAT FIND /I "JEMMEX" C:\AUTOEXEC.BAT
ECHO.
ECHO If a line above is not from BOOTCFG.BAT, remove it yourself (EDIT C:\AUTOEXEC.BAT) or run C:\RESET.BAT.
ECHO.
CHOICE /C:YN Delete the folders and files listed first
IF ERRORLEVEL 2 GOTO NO
IF ERRORLEVEL 1 GOTO GO
ECHO CHOICE is not available here. Nothing was deleted.
GOTO END

:GO
ECHO.
IF EXIST C:\JEMMEX\NUL DELTREE /Y C:\JEMMEX
IF EXIST C:\UCDD\NUL DELTREE /Y C:\UCDD
IF EXIST C:\OAKCD\NUL DELTREE /Y C:\OAKCD
IF EXIST C:\XCDROM\NUL DELTREE /Y C:\XCDROM
IF EXIST C:\CDSET\SHSUCDHD.EXE DEL C:\CDSET\SHSUCDHD.EXE
IF EXIST C:\CDSET\SHCDX86.COM DEL C:\CDSET\SHCDX86.COM
IF EXIST C:\CDSET\XCDROM.SYS DEL C:\CDSET\XCDROM.SYS
IF EXIST C:\CONFIG.NEW DEL C:\CONFIG.NEW
IF EXIST C:\AUTOEXEC.NEW DEL C:\AUTOEXEC.NEW
IF EXIST C:\AUTOEXEC.TMP DEL C:\AUTOEXEC.TMP
IF EXIST C:\CONFIG.NO DEL C:\CONFIG.NO
IF EXIST C:\AUTOEXEC.NO DEL C:\AUTOEXEC.NO
IF EXIST C:\BCDBG.BAT DEL C:\BCDBG.BAT
IF EXIST C:\JEMMEX\NUL ECHO Could not remove C:\JEMMEX (close it or restart and run CLEANUP again).
IF EXIST C:\UCDD\NUL ECHO Could not remove C:\UCDD (close it or restart and run CLEANUP again).
ECHO Done. Restart the PC before you run CDON.BAT again, then reinstall with SETUP.BAT and CDINST.BAT.
GOTO END

:NO
ECHO Nothing was deleted.
:END
SET CL_N=
