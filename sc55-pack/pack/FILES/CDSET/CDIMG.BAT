@ECHO OFF
REM ======================================================================
REM  CDIMG.BAT - pick a disc image (.ISO / .CUE / .MDM in C:\CDIMAGES) and mount it with uCDD
REM
REM    CDIMG              list the images
REM    CDIMG NAME.ISO     mount it   (CDIMG NAME also works: tries .ISO, .CUE, .MDM)
REM    CDIMG /U           unmount
REM  Run CDON.BAT first (once per session). DOS shows 8.3 names only: DOOM.ISO.
REM  A .CUE mounts its BIN (audio tracks too); .MDM is a list of discs (see UCDD-DOC.TXT).
REM ======================================================================
SET CDDIR=C:\CDSET
SET IMGDIR=C:\CDIMAGES
SET IMG=
IF "%1"=="" GOTO LIST
IF "%1"=="/U" GOTO UNMOUNT
IF "%1"=="/u" GOTO UNMOUNT
IF NOT EXIST %CDDIR%\UCDD.EXE GOTO NOTINST
IF NOT "%CDVM%"=="1" CALL %CDDIR%\CDON.BAT
IF EXIST %IMGDIR%\%1.ISO SET IMG=%IMGDIR%\%1.ISO
IF EXIST %IMGDIR%\%1.CUE SET IMG=%IMGDIR%\%1.CUE
IF EXIST %IMGDIR%\%1.MDM SET IMG=%IMGDIR%\%1.MDM
IF EXIST %IMGDIR%\%1 SET IMG=%IMGDIR%\%1
IF "%IMG%"=="" GOTO NOTFOUND
REM  like a real drive: the old disc comes out before the new one goes in
%CDDIR%\UCDD.EXE -unmount > NUL
ECHO Mounting %IMG% ...
%CDDIR%\UCDD.EXE -mount %IMG%
IF ERRORLEVEL 1 GOTO FAILED
ECHO %1> %CDDIR%\MOUNTED.FLG
ECHO Mounted. The virtual CD is drive M:.
GOTO END

:LIST
ECHO Disc images in %IMGDIR%:
ECHO.
IF EXIST %IMGDIR%\*.ISO DIR /B %IMGDIR%\*.ISO
IF EXIST %IMGDIR%\*.CUE DIR /B %IMGDIR%\*.CUE
IF EXIST %IMGDIR%\*.MDM DIR /B %IMGDIR%\*.MDM
ECHO.
ECHO Type  CDIMG name.ext  to mount one, or  CDIMG /U  to unmount.
GOTO END

:UNMOUNT
%CDDIR%\UCDD.EXE -unmount
IF EXIST %CDDIR%\MOUNTED.FLG DEL %CDDIR%\MOUNTED.FLG
GOTO END

:NOTFOUND
ECHO No image "%1" in %IMGDIR% (looked for .ISO, .CUE and .MDM). Run CDIMG to list them.
GOTO END
:NOTINST
ECHO uCDD is not installed in %CDDIR%. Run CDINST.BAT from the pack folder first.
GOTO END
:FAILED
ECHO uCDD could not mount the image (messages above). Close programs that use the drive and try again.
:END
SET CDDIR=
SET IMGDIR=
SET IMG=
