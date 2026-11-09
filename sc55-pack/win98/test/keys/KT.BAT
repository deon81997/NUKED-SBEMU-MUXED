@echo off
mode com1:9600,n,8,1 >nul
echo ==== KT %1 >COM1
C:\KEEN4\KEEN4E.EXE >COM1
echo ==== END >COM1
