@echo off
rem Windows-made NTFS + exFAT test volumes for fsread. Right-click, Run as administrator.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0mkvhd.ps1"
pause
