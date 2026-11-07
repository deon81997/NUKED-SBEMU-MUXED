@ECHO OFF
REM Comparison test: starts the OLDER SBEMU beta.25 (SBEMU25.EXE) with your saved settings.
REM Restart the PC between this and SC55.BAT - only one SBEMU can be loaded.
C:
CD \SC55
C:\HDPMI\HDPMI32I.EXE -r -x
IF EXIST SBOPTS.TXT SBEMU25.EXE /VMPU /VSCROM:ROMS @SBOPTS.TXT %1 %2 %3 %4
IF NOT EXIST SBOPTS.TXT SBEMU25.EXE /VMPU /VSCROM:ROMS %1 %2 %3 %4
