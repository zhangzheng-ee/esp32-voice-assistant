@echo off
if "%~1"=="" (
  echo.
  echo   Drag an image file onto this BAT file.
  echo   Or run:  img2h.bat  D:\path\to\image.jpg
  echo.
  pause
  exit /b
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0img2h.ps1" -ImagePath "%~1" -OutputPath "%~dpn1.h"
echo.
pause