@echo off
setlocal EnableExtensions
cd /d "%~dp0"

REM ---------------------------------------------------------------------------
REM ShellOS Imager — Windows EXE build (onefile + bundled firmware)
REM Uses imager\.venv_build with Python 3.11+ (or 3.10.4+). Your repo .venv is
REM not modified — avoids PyInstaller crashes on Python 3.10.0.
REM ---------------------------------------------------------------------------

set "PY311=%LOCALAPPDATA%\Programs\Python\Python311\python.exe"
set "PY312=%LOCALAPPDATA%\Programs\Python\Python312\python.exe"
set "PY310=%LOCALAPPDATA%\Programs\Python\Python310\python.exe"
set "PYEXE="

if exist "%PY311%" set "PYEXE=%PY311%"
if not defined PYEXE if exist "%PY312%" set "PYEXE=%PY312%"
if not defined PYEXE if exist "%PY310%" set "PYEXE=%PY310%"

if not defined PYEXE (
  where python >nul 2>&1 || (
    echo No Python found. Install Python 3.11 from https://www.python.org/downloads/windows/
    exit /b 1
  )
  for /f "delims=" %%I in ('where python 2^>nul') do set "PYEXE=%%I" & goto :after_where
)
:after_where

if not defined PYEXE (
  echo Could not resolve python.exe
  exit /b 1
)

"%PYEXE%" -c "import sys; v=sys.version_info; ok=(v.major>3) or (v.major==3 and v.minor>10) or (v.major==3 and v.minor==10 and v.micro>=4); sys.exit(0 if ok else 1)" 2>nul
if errorlevel 1 (
  echo.
  echo ERROR: PyInstaller needs Python 3.11.x or at least 3.10.4.
  echo Your current interpreter: 
  "%PYEXE%" -c "import sys; print(sys.executable); print(sys.version)"
  echo.
  echo Install Python 3.11 from python.org ^(enable "Add to PATH"^), then run this script again.
  echo Expected path: %PY311%
  echo.
  pause
  exit /b 1
)

if not exist ".venv_build\Scripts\python.exe" (
  echo Creating build venv: %CD%\.venv_build
  "%PYEXE%" -m venv .venv_build || exit /b 1
)

".venv_build\Scripts\python.exe" -m pip install -q --upgrade pip pyinstaller pyqt6 pyserial esptool || exit /b 1

REM Fail fast if firmware is not present
set "MISSING="
if not exist "firmware\esp32-cam\bootloader.bin" set "MISSING=1"
if not exist "firmware\esp32-cam\partition-table.bin" set "MISSING=1"
if not exist "firmware\esp32-cam\esp32_shell_os.bin" set "MISSING=1"
if not exist "firmware\esp32-c6\bootloader.bin" set "MISSING=1"
if not exist "firmware\esp32-c6\partition-table.bin" set "MISSING=1"
if not exist "firmware\esp32-c6\esp32_shell_os.bin" set "MISSING=1"

if defined MISSING (
  echo.
  echo ERROR: Firmware .bin files are missing. Copy ESP-IDF build outputs first.
  echo See: firmware\README.txt
  echo.
  pause
  exit /b 1
)

echo.
echo Building onefile EXE with: 
".venv_build\Scripts\python.exe" -c "import sys; print(sys.executable); print(sys.version)"
echo.

".venv_build\Scripts\python.exe" -m PyInstaller --noconfirm --clean --onefile --windowed --name ShellOSImager ^
  --add-data "firmware;firmware" ^
  --hidden-import serial.tools.list_ports ^
  --collect-submodules esptool ^
  shellos_imager.py
if errorlevel 1 (
  echo PyInstaller failed.
  pause
  exit /b 1
)

echo.
echo ============================================================
echo  OK:  dist\ShellOSImager.exe
echo  ^(self-contained; firmware embedded — copy only this file^)
echo ============================================================
echo.
pause
endlocal
