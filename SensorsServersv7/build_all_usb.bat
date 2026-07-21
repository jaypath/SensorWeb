@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Build every non-OTA PlatformIO environment from platformio.ini and copy
rem firmware.bin to firmware\<devicename>-<x.x.x>.bin (flat folder, SD /Firmware layout).
rem
rem Environments and device names are discovered from platformio.ini:
rem   - skip any env whose name contains _OTA or uses upload_protocol=espota
rem   - device name from -D _MYDEVICENAME="..." (fallback: env name without _USB)
rem Version from CONFIG_APP_PROJECT_VER.
rem
rem Usage:
rem   build_all_usb.bat                        Build all non-OTA environments
rem   build_all_usb.bat Den_USB                Build one env (device name from .ini)
rem   build_all_usb.bat Den_USB Den            Build one env (explicit device name)
rem   build_all_usb.bat PleasantWeather        Friendly alias (see :lookup_alias)

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

set "PIO=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
if not exist "%PIO%" (
  echo ERROR: PlatformIO not found at %PIO%
  exit /b 1
)

set "UTIL=%SCRIPT_DIR%build_usb_util.ps1"
if not exist "%UTIL%" (
  echo ERROR: Helper not found: %UTIL%
  exit /b 1
)

set "FW_ROOT=%SCRIPT_DIR%firmware"
set "TARGET_LIST=%TEMP%\build_usb_targets_%RANDOM%.txt"
set "FW_VER="
set "FAIL_COUNT=0"
set "OK_COUNT=0"
set "SKIP_COUNT=0"

for /f "usebackq delims=" %%V in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%UTIL%" -Mode version`) do set "FW_VER=%%V"
if "!FW_VER!"=="" (
  echo ERROR: Could not read CONFIG_APP_PROJECT_VER from platformio.ini
  exit /b 1
)

if not exist "%FW_ROOT%" mkdir "%FW_ROOT%"

if not "%~1"=="" (
  set "SINGLE_ENV="
  set "SINGLE_DEVICE="
  call :lookup_alias "%~1"
  if "!SINGLE_ENV!"=="" set "SINGLE_ENV=%~1"
  if "%~2"=="" (
    if "!SINGLE_DEVICE!"=="" (
      for /f "usebackq delims=" %%D in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%UTIL%" -Mode device -EnvName "!SINGLE_ENV!"`) do set "SINGLE_DEVICE=%%D"
    )
  ) else (
    set "SINGLE_DEVICE=%~2"
  )
  if "!SINGLE_DEVICE!"=="" (
    echo ERROR: Unknown target "%~1". Use a PlatformIO env name ^(e.g. Den_USB^) or alias ^(e.g. PleasantWeather^).
    echo Pass device name as second argument if needed.
    exit /b 1
  )
  call :build_one "!SINGLE_ENV!" "!SINGLE_DEVICE!"
  goto :summary
)

echo.
echo === Build all non-OTA environments ^(from platformio.ini^) ===
echo Firmware output root: %FW_ROOT%
echo Firmware version:   %FW_VER%
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%UTIL%" -Mode list > "%TARGET_LIST%"
if errorlevel 1 (
  echo ERROR: Failed to scan platformio.ini for USB/non-OTA targets.
  if exist "%TARGET_LIST%" del "%TARGET_LIST%" >nul 2>&1
  exit /b 1
)

for /f "usebackq tokens=1,2 delims=|" %%A in ("%TARGET_LIST%") do (
  call :build_one "%%A" "%%B"
)

if exist "%TARGET_LIST%" del "%TARGET_LIST%" >nul 2>&1

goto :summary

rem Friendly names (first argument only) — map to PlatformIO env names in .ini
:lookup_alias
set "SINGLE_ENV="
set "SINGLE_DEVICE="
if /i "%~1"=="PleasantServer" set "SINGLE_ENV=wthrbase_USB_MainServer" & set "SINGLE_DEVICE=PleasantWeather"
if /i "%~1"=="PleasantWeather" set "SINGLE_ENV=wthrbase_USB_MainServer" & set "SINGLE_DEVICE=PleasantWeather"
if /i "%~1"=="WeatherGSheet" set "SINGLE_ENV=wthrgsheet_USB" & set "SINGLE_DEVICE=WeatherGSheet"
if /i "%~1"=="PleasantBackup" set "SINGLE_ENV=wthrbase_USB"
if /i "%~1"=="PleasantBackupServer" set "SINGLE_ENV=wthrbase_USB"
exit /b 0

:build_one
set "ENV=%~1"
set "DEVICE=%~2"
if "%ENV%"=="" exit /b 0
if "%DEVICE%"=="" (
  echo ERROR: Missing device name for environment %ENV%
  set /a FAIL_COUNT+=1
  exit /b 1
)

set "DEST_FILE=%FW_ROOT%\%DEVICE%-%FW_VER%.bin"

echo.
echo ----------------------------------------
echo [%ENV%] -^> %DEST_FILE%
echo ----------------------------------------

"%PIO%" run -e %ENV%
if errorlevel 1 (
  echo ERROR: Build failed for %ENV%
  set /a FAIL_COUNT+=1
  exit /b 1
)

set "SRC=.pio\build\%ENV%\firmware.bin"
if not exist "%SRC%" (
  echo ERROR: %SRC% not found after build
  set /a FAIL_COUNT+=1
  exit /b 1
)

copy /Y "%SRC%" "%DEST_FILE%" >nul
if errorlevel 1 (
  echo ERROR: Failed to copy firmware for %ENV%
  set /a FAIL_COUNT+=1
  exit /b 1
)

echo OK: copied to %DEST_FILE%
set /a OK_COUNT+=1
exit /b 0

:summary
echo.
echo === Done ===
echo Successful: %OK_COUNT%
echo Failed:     %FAIL_COUNT%
echo.

if %FAIL_COUNT% gtr 0 (
  endlocal & exit /b 1
)
endlocal & exit /b 0
