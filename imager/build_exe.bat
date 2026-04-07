@echo off
cd /d "%~dp0"
where python >nul 2>&1 || (echo Install Python 3.10+ and try again. & exit /b 1)
python -m pip install -q pyinstaller pyqt6 pyserial esptool
if not exist "firmware\bootloader.bin" (
  echo.
  echo WARNING: Copy bootloader.bin, partition-table.bin, esp32_shell_os.bin into firmware\ first.
  echo See firmware\README.txt
  echo.
  pause
)
pyinstaller --noconfirm --windowed --name ShellOSImager ^
  --add-data "firmware;firmware" ^
  --hidden-import serial.tools.list_ports ^
  --collect-submodules esptool ^
  shellos_imager.py
echo.
echo ============================================================
echo  RUN THIS EXE (complete app + Python DLLs + firmware):
echo    dist\ShellOSImager\ShellOSImager.exe
echo.
echo  Do NOT run anything under build\  (incomplete PyInstaller
echo  temp output — it will fail with "Failed to load python311.dll")
echo ============================================================
echo.
pause
