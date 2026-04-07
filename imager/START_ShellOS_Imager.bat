@echo off
REM Always runs the packaged app from dist\ (never use build\ — that exe is incomplete).
cd /d "%~dp0"
set "EXE=%~dp0dist\ShellOSImager\ShellOSImager.exe"
if not exist "%EXE%" (
  echo.
  echo ShellOS Imager was not found at:
  echo   %EXE%
  echo.
  echo Build it first: double-click build_exe.bat
  echo.
  pause
  exit /b 1
)
start "" "%EXE%"
