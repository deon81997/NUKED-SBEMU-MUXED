@ECHO OFF
REM CDON.BAT - turns on the virtual CD drive for this DOS session (nothing is started at boot).
REM   1. loads uCDD (mount-only build: no sound code, no memory manager needed)
REM   2. gives the virtual drive the letter M: with SHSUCDX (never A:/B: - those are the floppy)
REM   3. real CD-ROM: if MSCDEX already serves it (BOOTCFG.BAT, a Windows 98 DOS-mode setup),
REM      SHSUCDX installs next to it (/I) and D: keeps working. If only a CD-ROM driver is loaded
REM      (XCDROM, VIDE-CDD, LG, Oak) SHSUCDX gives that drive a letter too, because MSCDEX
REM      cannot be loaded any more once SHSUCDX is in memory.
IF "%CDVM%"=="1" GOTO ALREADY
IF EXIST C:\CDSET\MOUNTED.FLG DEL C:\CDSET\MOUNTED.FLG
REM  if uCDD is already loaded this reports it; the SHSUCDX step below fails if it is really missing
C:\CDSET\UCDD.EXE -install
REM  /R- : files on the CD are not marked read-only (installers that keep the attribute
REM  otherwise fail with "Access denied" when they update the copied files)
REM  drop any earlier drive letter (an old install used A:), then assign M:
C:\CDSET\SHSUCDX.COM /U > NUL
C:\CDSET\CDCHK.COM
IF ERRORLEVEL 1 GOTO WITHMSCD
C:\CDSET\SHSUCDX.COM /R- /D:?XCD0001,D /D:?MSCD001,D /D:?MSCD000,D /D:?OAKCD,D /D:UCDD0001,M
IF ERRORLEVEL 246 GOTO FAILED
GOTO OK
:WITHMSCD
C:\CDSET\SHSUCDX.COM /R- /I /D:UCDD0001,M
IF ERRORLEVEL 246 GOTO FAILED
:OK
SET CDVM=1
GOTO END
:ALREADY
ECHO The virtual CD drive is already on in this session.
GOTO END
:FAILED
ECHO.
ECHO The virtual CD drive could not be started (see the messages above).
ECHO "Need more drive letters": add the line  LASTDRIVE=Z  to CONFIG.SYS, then restart.
ECHO Anything else: restart the PC to plain DOS and try again (C:\CDSET\CDDIAG.BAT checks each step).
:END
