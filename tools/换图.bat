@echo off
title Change Image
rem Double-click: pop up a file picker, generate plantest\image.h
rem Then click Upload in Arduino IDE. No code change needed.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0pick.ps1"

echo.
pause