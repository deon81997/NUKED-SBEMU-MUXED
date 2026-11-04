@ECHO OFF
REM ======================================================================
REM  CDINST.BAT - installs the virtual CD drive (uCDD, mount-only build) and the disc menu.
REM  Run it from this folder (the one containing FILES).   CDINST [drive]   default C
REM  It only copies files. CONFIG.SYS and AUTOEXEC.BAT are NOT touched; nothing starts by itself.
REM  After it: in DOS run  C:\CDSET\CDON.BAT  (or just  C:\CDSET\GUI.BAT ) once per session.
REM ======================================================================
SET DRV=%1
IF "%DRV%"=="" SET DRV=C
IF NOT EXIST %DRV%:\NUL GOTO NODRIVE
IF NOT EXIST FILES\CDSET\UCDD.EXE GOTO NOFILES
IF NOT EXIST FILES\CDSET\SHSUCDX.COM GOTO NOFILES
ECHO Copying files to %DRV%:\CDSET ...
IF NOT EXIST %DRV%:\CDSET\NUL MD %DRV%:\CDSET
IF NOT EXIST %DRV%:\CDIMAGES\NUL MD %DRV%:\CDIMAGES
IF NOT EXIST %DRV%:\HDPMI\NUL MD %DRV%:\HDPMI
COPY FILES\CDSET\*.* %DRV%:\CDSET > NUL
COPY FILES\CDIMAGES\*.* %DRV%:\CDIMAGES > NUL
IF NOT EXIST %DRV%:\HDPMI\HDPMI32I.EXE COPY FILES\HDPMI\HDPMI32I.EXE %DRV%:\HDPMI > NUL
IF NOT EXIST %DRV%:\CDSET\UCDD.EXE GOTO COPYFAIL
IF NOT EXIST %DRV%:\CDSET\SHSUCDX.COM GOTO COPYFAIL
IF NOT EXIST %DRV%:\CDSET\GUI.BAT GOTO COPYFAIL
IF NOT EXIST %DRV%:\CDSET\HYBRID.BAT GOTO COPYFAIL
ECHO @ECHO OFF> %DRV%:\MOUNT.BAT
ECHO CALL %DRV%:\CDSET\GUI.BAT>> %DRV%:\MOUNT.BAT
ECHO %DRV%:>> %DRV%:\MOUNT.BAT
ECHO CD \>> %DRV%:\MOUNT.BAT
ECHO.
ECHO Done.  Put your .ISO / .CUE / .MDM images (8.3 names) in %DRV%:\CDIMAGES, then in DOS:
ECHO   MOUNT  (from %DRV%:\)  or  %DRV%:\CDSET\GUI.BAT        menu (arrow keys, ENTER mounts, U unmounts)
ECHO   %DRV%:\CDSET\CDIMG.BAT      command line (CDIMG lists, CDIMG NAME.ISO mounts)
ECHO.
ECHO CD-AUDIO THROUGH YOUR SOUND CARD (experimental):  run  %DRV%:\CDSET\HYBRID.BAT  instead of SC55.BAT.
ECHO   It loads the CD driver first, then SBEMU with /VCDA:100. A test disc is in %DRV%:\CDIMAGES (TEST.CUE).
ECHO   Then:  CDIMG TEST.CUE   and   %DRV%:\CDSET\CDPLAY.COM M: 2     (the virtual CD is drive M:)
GOTO END
:NODRIVE
ECHO Drive %DRV%: does not exist.
GOTO END
:NOFILES
ECHO Run CDINST from the folder that contains the FILES folder.
GOTO END
:COPYFAIL
ECHO A copy failed or %DRV%:\CDSET could not be created. Is %DRV%: writable and has it free space?
:END
SET DRV=
