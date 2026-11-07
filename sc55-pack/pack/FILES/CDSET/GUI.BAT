@ECHO OFF
REM GUI.BAT - the CD images screen: mount .CUE/.BIN, .ISO or .MDM disc images from C:\CDIMAGES on drive M:
REM   GUI [sc55 folder]      (the SC-55 menu passes its folder so the screen uses your style)
REM CDGUI.EXE only picks; the uCDD command it chooses is run here from plain DOS (CDRUN.BAT),
REM then the screen comes back and shows the result. Esc in the screen ends this.
IF NOT EXIST C:\CDSET\CDGUI.EXE GOTO NOTINST
IF NOT EXIST C:\CDSET\UCDD.EXE GOTO NOTINST
IF NOT EXIST C:\CDIMAGES\NUL MD C:\CDIMAGES
IF NOT "%CDVM%"=="1" CALL C:\CDSET\CDON.BAT
REM CDGUI.EXE is a 32-bit program and needs a DPMI host; HDPMI32I (also used by the SC-55) is loaded
IF EXIST C:\HDPMI\HDPMI32I.EXE C:\HDPMI\HDPMI32I.EXE -r -x > NUL
SET CDHOME=%1
IF "%CDHOME%"=="" SET CDHOME=C:\SC55
:AGAIN
IF EXIST C:\CDSET\CDRUN.BAT DEL C:\CDSET\CDRUN.BAT
C:\CDSET\CDGUI.EXE %CDHOME% C:\CDIMAGES C:\CDSET
IF NOT EXIST C:\CDSET\CDRUN.BAT GOTO END
CALL C:\CDSET\CDRUN.BAT
GOTO AGAIN
:NOTINST
ECHO The CD tools are not installed in C:\CDSET yet. Run CDINST.BAT from the pack folder first.
PAUSE
:END
IF EXIST C:\CDSET\CDRUN.BAT DEL C:\CDSET\CDRUN.BAT
SET CDHOME=
