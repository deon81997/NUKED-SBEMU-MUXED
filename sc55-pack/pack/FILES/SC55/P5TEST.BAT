@ECHO OFF
REM P5TEST N - find which beta.113-119 change freezes a PC (first case: ASUS P5PE-VM + Audigy, beta.118).
REM Run it in this folder instead of SC55.BAT, then do what froze before (MENU, a game). Reboot after a freeze
REM and run the next number: the logs of the last run are saved first to P5T<N>.LOG (SBDETAIL + SBTRACE).
REM   1  SC55 as shipped: card watchdog auto (off for the Audigy), CD music on
REM   2  card watchdog forced on (/CARDWD:1, the beta.115-118 behaviour), CD music on
REM   3  card watchdog forced on, uCDD + CD music off
REM   4  card watchdog off, uCDD + CD music off (the beta.112 start-up)
IF "%1"=="" GOTO HELP
IF NOT EXIST P5TLAST.TXT GOTO RUN
FOR %%L IN (1 2 3 4) DO IF EXIST P5TLAST.%%L COPY /B SBDETAIL.TXT+SBTRACE.TXT P5T%%L.LOG > NUL
FOR %%L IN (1 2 3 4) DO IF EXIST P5TLAST.%%L DEL P5TLAST.%%L
DEL P5TLAST.TXT
:RUN
ECHO P5TEST %1 > P5TLAST.TXT
ECHO x > P5TLAST.%1
SET SBNOCDA=
IF "%1"=="1" CALL SC55.BAT
IF "%1"=="2" CALL SC55.BAT /CARDWD:1
IF "%1"=="3" SET SBNOCDA=1
IF "%1"=="3" CALL SC55.BAT /CARDWD:1
IF "%1"=="4" SET SBNOCDA=1
IF "%1"=="4" CALL SC55.BAT /CARDWD:0
SET SBNOCDA=
ECHO P5TEST %1: SBEMU loaded. Now start what froze before (MENU, a game). After a freeze: reboot, P5TEST next number.
GOTO END
:HELP
ECHO Usage: P5TEST 1, 2, 3 or 4 (see the REM lines in P5TEST.BAT). Send the P5T*.LOG files afterwards.
:END
