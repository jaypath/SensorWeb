@echo off
setlocal EnableExtensions

rem Wipe OTA metadata and app1 so the ESP32-S3 boots from app0 (ota_0).
rem Partition layout: partitions_wt32sc01_ota_no_spiffs.csv
rem   otadata  0xD000   size 0x2000
rem   app0     0x10000  size 0x1F0000
rem   app1     0x200000 size 0x1F0000

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

set "PIO=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
if not exist "%PIO%" (
  echo ERROR: PlatformIO not found at %PIO%
  exit /b 1
)

set "PORT=%~1"
set "UPLOAD_ENV=%~2"

if "%PORT%"=="" (
  echo.
  echo Usage: erase_ota_slot0.bat COMx [upload_env]
  echo.
  echo   COMx         Serial port, e.g. COM7
  echo   upload_env   Optional PlatformIO environment to flash app0 after wipe
  echo                e.g. wthrbase_USB_BACKUPSERVER
  echo.
  echo Example:
  echo   erase_ota_slot0.bat COM7
  echo   erase_ota_slot0.bat COM7 wthrbase_USB_BACKUPSERVER
  echo.
  echo Available ports:
  "%PIO%" device list
  exit /b 1
)

echo.
echo === Erase OTA and force boot from slot 0 (app0) ===
echo Port: %PORT%
echo.

echo [1/2] Erasing otadata at 0xD000 ...
"%PIO%" pkg exec -p "tool-esptoolpy" -- esptool.py --chip esp32s3 --port %PORT% erase_region 0xd000 0x2000
if errorlevel 1 (
  echo ERROR: Failed to erase otadata.
  exit /b 1
)

echo [2/2] Erasing app1 (ota_1) at 0x200000 ...
"%PIO%" pkg exec -p "tool-esptoolpy" -- esptool.py --chip esp32s3 --port %PORT% erase_region 0x200000 0x1F0000
if errorlevel 1 (
  echo ERROR: Failed to erase app1.
  exit /b 1
)

echo.
echo Done. otadata and app1 are wiped.
echo The device will boot from app0 if it contains a valid firmware image.
echo.

if not "%UPLOAD_ENV%"=="" (
  echo Uploading firmware to app0 using environment: %UPLOAD_ENV%
  "%PIO%" run -t upload -e %UPLOAD_ENV% --upload-port %PORT%
  if errorlevel 1 (
    echo ERROR: Upload failed.
    exit /b 1
  )
  echo Upload complete.
) else (
  echo To flash app0 now, run:
  echo   "%PIO%" run -t upload -e wthrbase_USB_BACKUPSERVER --upload-port %PORT%
)

endlocal
exit /b 0
