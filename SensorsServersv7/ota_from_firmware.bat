@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Upload prebuilt firmware\*.bin to devices via espota, matching bin device name
rem to platformio.ini OTA environments (_MYDEVICENAME, including inherited USB flags).
rem
rem Expected bin names (from build_all_usb.bat):
rem   firmware\<DeviceName>-<x.y.z>.bin
rem   e.g. firmware\FamRm-9.4.21.bin
rem
rem Usage:
rem   ota_from_firmware.bat
rem   ota_from_firmware.bat 192.168.68.
rem   ota_from_firmware.bat 192.168.68. FamRm
rem   ota_from_firmware.bat "" FamRm
rem
rem Args:
rem   %1  optional IP prefix (e.g. 192.168.68.) — empty = all OTA IPs in .ini
rem   %2  optional device or OTA env filter (e.g. FamRm or FamRm_OTA)

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

set "PIO_PYTHON=%USERPROFILE%\.platformio\penv\Scripts\python.exe"
set "UTIL=%SCRIPT_DIR%ota_firmware_util.ps1"
set "FW_ROOT=%SCRIPT_DIR%firmware"
set "TARGET_LIST=%TEMP%\ota_from_fw_%RANDOM%.txt"

rem Locate espota.py (package layout varies by PlatformIO / Arduino-ESP32 version).
set "ESPOTA="
if exist "%USERPROFILE%\.platformio\packages\tool-espotapy\espota.py" (
  set "ESPOTA=%USERPROFILE%\.platformio\packages\tool-espotapy\espota.py"
)
if "!ESPOTA!"=="" if exist "%USERPROFILE%\.platformio\packages\framework-arduinoespressif32\tools\espota.py" (
  set "ESPOTA=%USERPROFILE%\.platformio\packages\framework-arduinoespressif32\tools\espota.py"
)
if "!ESPOTA!"=="" if exist "%USERPROFILE%\.platformio\packages\framework-arduinoespressif32-libs\tools\espota.py" (
  set "ESPOTA=%USERPROFILE%\.platformio\packages\framework-arduinoespressif32-libs\tools\espota.py"
)
if "!ESPOTA!"=="" (
  for /f "usebackq delims=" %%P in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-ChildItem -Path (Join-Path $env:USERPROFILE '.platformio\packages') -Recurse -Filter espota.py -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName"`) do set "ESPOTA=%%P"
)

if not exist "%UTIL%" (
  echo ERROR: Helper not found: %UTIL%
  exit /b 1
)
if not exist "%PIO_PYTHON%" (
  echo ERROR: PlatformIO Python not found at %PIO_PYTHON%
  exit /b 1
)
if "!ESPOTA!"=="" (
  echo ERROR: Could not find espota.py under %%USERPROFILE%%\.platformio\packages
  echo Tried tool-espotapy and framework-arduinoespressif32\tools.
  exit /b 1
)
if not exist "!ESPOTA!" (
  echo ERROR: espota.py path invalid: !ESPOTA!
  exit /b 1
)
echo Using espota: !ESPOTA!
if not exist "%FW_ROOT%" (
  echo ERROR: Firmware folder not found: %FW_ROOT%
  echo Run build_all_usb.bat first to produce firmware\^<Device^>-^<ver^>.bin files.
  exit /b 1
)

set "IP_PREFIX=%~1"
set "DEVICE_FILTER=%~2"

set "FW_VER="
for /f "usebackq delims=" %%V in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%UTIL%" -Mode version`) do set "FW_VER=%%V"
if "!FW_VER!"=="" (
  echo ERROR: Could not read CONFIG_APP_PROJECT_VER from platformio.ini
  exit /b 1
)

echo.
echo === OTA from firmware folder ===
echo Firmware dir: %FW_ROOT%
echo Prefer ver:   !FW_VER!
if not "!IP_PREFIX!"=="" echo IP prefix:    !IP_PREFIX!
if not "!DEVICE_FILTER!"=="" echo Device filter:!DEVICE_FILTER!
echo.

if "!IP_PREFIX!"=="" if "!DEVICE_FILTER!"=="" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%UTIL%" -Mode list -PreferVersion "!FW_VER!" > "%TARGET_LIST%"
) else if "!DEVICE_FILTER!"=="" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%UTIL%" -Mode list -PreferVersion "!FW_VER!" -IpPrefix "!IP_PREFIX!" > "%TARGET_LIST%"
) else if "!IP_PREFIX!"=="" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%UTIL%" -Mode list -PreferVersion "!FW_VER!" -DeviceFilter "!DEVICE_FILTER!" > "%TARGET_LIST%"
) else (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%UTIL%" -Mode list -PreferVersion "!FW_VER!" -IpPrefix "!IP_PREFIX!" -DeviceFilter "!DEVICE_FILTER!" > "%TARGET_LIST%"
)
if errorlevel 1 (
  echo ERROR: Failed to match firmware bins to OTA environments.
  if exist "%TARGET_LIST%" del "%TARGET_LIST%" >nul 2>&1
  exit /b 1
)

set "OK_COUNT=0"
set "FAIL_COUNT=0"
set "SKIP_COUNT=0"
set "MATCH_COUNT=0"

rem OTA|env|ip|auth|device|binPath|binVer|why
rem SKIP|env|ip|auth|device||why
for /f "usebackq tokens=1-8 delims=|" %%A in ("%TARGET_LIST%") do (
  if /i "%%A"=="SKIP" (
    set /a SKIP_COUNT+=1
    echo SKIP: %%E ^(%%B @ %%C^) — %%G
  ) else if /i "%%A"=="OTA" (
    set /a MATCH_COUNT+=1
    call :do_ota "%%B" "%%C" "%%D" "%%E" "%%F" "%%G" "%%H"
  )
)

if exist "%TARGET_LIST%" del "%TARGET_LIST%" >nul 2>&1

echo.
echo === Done ===
echo Matched with bin: !MATCH_COUNT!
echo Skipped ^(no bin^): !SKIP_COUNT!
echo Successful:       !OK_COUNT!
echo Failed:           !FAIL_COUNT!
echo.

if !FAIL_COUNT! gtr 0 (
  endlocal ^& exit /b 1
)
endlocal ^& exit /b 0

:do_ota
set "ENV=%~1"
set "IP=%~2"
set "AUTH=%~3"
set "DEVICE=%~4"
set "BIN=%~5"
set "BINVER=%~6"
set "WHY=%~7"
if "!ENV!"=="" exit /b 0
if not exist "!BIN!" (
  echo FAIL: !ENV! — bin missing: !BIN!
  set /a FAIL_COUNT+=1
  exit /b 1
)

echo.
echo ----------------------------------------
echo OTA: !ENV! / !DEVICE! @ !IP!
echo Bin: !BIN!  ^(!BINVER! — !WHY!^)
echo ----------------------------------------

"%PIO_PYTHON%" "!ESPOTA!" -i "!IP!" -p 3232 -a "!AUTH!" -f "!BIN!" --progress
if errorlevel 1 (
  echo FAIL: !ENV! @ !IP!
  set /a FAIL_COUNT+=1
  exit /b 1
)

echo OK: !ENV! ^(!DEVICE!^)
set /a OK_COUNT+=1
exit /b 0
