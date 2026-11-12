@ECHO OFF
REM SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
REM CDDIAG.BAT - runs the virtual CD steps one at a time so you can see which one fails.
REM Use it right after a restart. Press a key between steps; write down the step where the error appears.
SET CDVM=
ECHO.
ECHO STEP 1 of 5: load the driver      (C:\CDSET\UCDD.EXE -install)
PAUSE
C:\CDSET\UCDD.EXE -install
IF ERRORLEVEL 1 ECHO ** step 1 reported an error
ECHO.
ECHO STEP 2 of 5: give it a drive letter   (SHSUCDX.COM /R- /D:UCDD0001,M /P:M)
PAUSE
C:\CDSET\SHSUCDX.COM /R- /D:UCDD0001,M /P:M
IF ERRORLEVEL 246 ECHO ** step 2 reported an error
ECHO.
ECHO STEP 3 of 5: mount the test disc   (UCDD.EXE -mount C:\CDIMAGES\TEST.CUE)
IF NOT EXIST C:\CDIMAGES\TEST.CUE ECHO ** C:\CDIMAGES\TEST.CUE is missing - run CDINST.BAT again
PAUSE
C:\CDSET\UCDD.EXE -mount C:\CDIMAGES\TEST.CUE
IF ERRORLEVEL 1 ECHO ** step 3 reported an error
ECHO.
ECHO STEP 4 of 5: which letter is the drive?   (UCDD.EXE -drive)
PAUSE
C:\CDSET\UCDD.EXE -drive
ECHO.
ECHO STEP 5 of 5: type  DIR x:  yourself (x = the letter above). Does it list TEST.TXT or fail?
ECHO Then try:  C:\CDSET\CDPLAY.COM x: 2
