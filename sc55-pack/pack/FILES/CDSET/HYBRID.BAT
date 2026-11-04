@ECHO OFF
REM HYBRID.BAT - virtual CD drive + SC-55, with CD-Audio from CUE/BIN images played through SBEMU
REM (AC'97 etc.). Use it instead of SC55.BAT for games with CD music. Extra SBEMU options can follow.
IF NOT EXIST C:\SC55\SBEMU.EXE GOTO NOSC
IF NOT "%CDVM%"=="1" CALL C:\CDSET\CDON.BAT
C:
CD \SC55
C:\HDPMI\HDPMI32I.EXE -r -x
SET SBEMULOG=C:\SC55\SBLOG.TXT
CALL C:\SC55\SC55RUN.BAT /VCDA:100 %1 %2 %3
GOTO END
:NOSC
ECHO Run SETUP.BAT from the pack folder first (it installs SBEMU in C:\SC55).
:END
