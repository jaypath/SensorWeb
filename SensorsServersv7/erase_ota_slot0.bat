@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Wipe OTA metadata and app1 so the device boots from app0 (ota_0).
rem Partition layout (same offsets on ESP32 and ESP32-S3 in this project):
rem   partitions_ota_no_spiffs.csv / partitions_wt32sc01_ota_no_spiffs.csv
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

set "PORT="
set "CHIP=auto"
set "UPLOAD_ENV="

:parse_args
if "%~1"=="" goto args_done

set "ARG=%~1"

if /i "!ARG!"=="--help" goto show_usage
if /i "!ARG!"=="-h" goto show_usage
if /i "!ARG!"=="--chip" (
  if "%~2"=="" (
    echo ERROR: --chip requires esp32, esp32s3, or auto.
    exit /b 1
  )
  set "CHIP=%~2"
  shift
  shift
  goto parse_args
)
if /i "!ARG!"=="-c" (
  if "%~2"=="" (
    echo ERROR: -c requires esp32, esp32s3, or auto.
    exit /b 1
  )
  set "CHIP=%~2"
  shift
  shift
  goto parse_args
)

echo !ARG! | findstr /r /i "^--chip=.*" >nul
if not errorlevel 1 (
  for /f "tokens=1,* delims==" %%A in ("!ARG!") do set "CHIP=%%B"
  shift
  goto parse_args
)

if "!PORT!"=="" (
  set "PORT=!ARG!"
  shift
  goto parse_args
)

if "!UPLOAD_ENV!"=="" (
  set "UPLOAD_ENV=!ARG!"
  shift
  goto parse_args
)

echo ERROR: Unexpected argument: !ARG!
goto show_usage

:args_done
if "!PORT!"=="" goto show_usage

if /i not "!CHIP!"=="auto" if /i not "!CHIP!"=="esp32" if /i not "!CHIP!"=="esp32s3" (
  echo ERROR: Invalid chip "!CHIP!". Use esp32, esp32s3, or auto.
  exit /b 1
)

echo.
echo === Erase OTA and force boot from slot 0 (app0) ===
echo Port: !PORT!
echo Chip: !CHIP!
echo.

if /i "!CHIP!"=="auto" (
  echo Detecting chip type ...
  for /f "usebackq delims=" %%L in (`"%PIO%" pkg exec -p tool-esptoolpy -- esptool.py --chip !CHIP! --port !PORT! chip-id 2^>^&1`) do (
    echo   %%L
    set "LINE=%%L"
    echo !LINE! | findstr /i "ESP32-S3" >nul && set "DETECTED=esp32s3"
    if not defined DETECTED (
      echo !LINE! | findstr /i "Chip is ESP32" >nul
      if not errorlevel 1 (
        echo !LINE! | findstr /i "ESP32-S3" >nul
        if errorlevel 1 set "DETECTED=esp32"
      )
    )
  )
  if defined DETECTED (
    echo Using detected chip: !DETECTED!
    set "CHIP=!DETECTED!"
  ) else (
    echo WARNING: Could not identify chip; continuing with --chip auto.
  )
  echo.
)

echo [1/2] Erasing otadata at 0xD000 ...
call :run_esptool erase_region 0xd000 0x2000
if errorlevel 1 (
  echo ERROR: Failed to erase otadata.
  echo Tip: Hold BOOT, tap RESET, release BOOT, then retry.
  echo Tip: If auto-detect failed, pass --chip esp32 or --chip esp32s3 explicitly.
  exit /b 1
)

echo [2/2] Erasing app1 (ota_1) at 0x200000 ...
call :run_esptool erase_region 0x200000 0x1F0000
if errorlevel 1 (
  echo ERROR: Failed to erase app1.
  exit /b 1
)

echo.
echo Done. otadata and app1 are wiped.
echo The device will boot from app0 if it contains a valid firmware image.
echo.

if not "!UPLOAD_ENV!"=="" (
  echo Uploading firmware to app0 using environment: !UPLOAD_ENV!
  "%PIO%" run -t upload -e !UPLOAD_ENV! --upload-port !PORT!
  if errorlevel 1 (
    echo ERROR: Upload failed.
    exit /b 1
  )
  echo Upload complete.
) else (
  echo To flash app0 now, run for example:
  echo   "%PIO%" run -t upload -e MastBR_USB --upload-port !PORT!
)

endlocal
exit /b 0

:run_esptool
"%PIO%" pkg exec -p tool-esptoolpy -- esptool.py --chip !CHIP! --port !PORT! %*
exit /b %errorlevel%

:show_usage
echo.
echo Usage: erase_ota_slot0.bat COMx [options] [upload_env]
echo.
echo   COMx         Serial port, e.g. COM7
echo   upload_env   Optional PlatformIO environment to flash app0 after wipe
echo                e.g. MastBR_USB or wthrbase_USB_BACKUPSERVER
echo.
echo Options:
echo   --chip esp32^|esp32s3^|auto   Target chip (default: auto)
echo   -c esp32^|esp32s3^|auto        Short form of --chip
echo   --chip=esp32^|esp32s3^|auto
echo   --help, -h                     Show this help
echo.
echo Examples:
echo   erase_ota_slot0.bat COM7
echo   erase_ota_slot0.bat COM7 MastBR_USB
echo   erase_ota_slot0.bat COM7 --chip esp32
echo   erase_ota_slot0.bat COM7 --chip esp32s3 MastBR_USB
echo   erase_ota_slot0.bat --chip esp32 COM7
echo.
echo OTA partition offsets are the same for ESP32 and ESP32-S3 in this project.
echo esptool auto-detects the chip by default; use --chip if detection fails.
echo.
echo Available ports:
"%PIO%" device list
exit /b 1
