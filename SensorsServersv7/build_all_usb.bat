@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Build every *_USB PlatformIO environment (skips *_OTA) and copy firmware.bin
rem to firmware\<devicename>-<x.x.x>.bin (flat folder, matches SD /Firmware layout).
rem
rem Version is read from CONFIG_APP_PROJECT_VER in platformio.ini.
rem
rem Usage:
rem   build_all_usb.bat                        Build all USB environments
rem   build_all_usb.bat Den_USB                Build one env (device name looked up)
rem   build_all_usb.bat Den_USB Den            Build one env (explicit device name)
rem   build_all_usb.bat PleasantServer         Friendly alias (see :lookup_alias)

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

set "PIO=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
if not exist "%PIO%" (
  echo ERROR: PlatformIO not found at %PIO%
  exit /b 1
)

set "FW_ROOT=%SCRIPT_DIR%firmware"
set "FW_VER="
set "FAIL_COUNT=0"
set "OK_COUNT=0"

for /f "usebackq delims=" %%V in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%get_fw_version.ps1"`) do set "FW_VER=%%V"
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
    if "!SINGLE_DEVICE!"=="" call :lookup_device "!SINGLE_ENV!"
  ) else (
    set "SINGLE_DEVICE=%~2"
  )
  if "!SINGLE_DEVICE!"=="" (
    echo ERROR: Unknown target "%~1". Use a PlatformIO env name ^(e.g. Den_USB^) or alias ^(e.g. PleasantServer^).
    echo Pass device name as second argument if needed.
    exit /b 1
  )
  call :build_one "!SINGLE_ENV!" "!SINGLE_DEVICE!"
  goto :summary
)

echo.
echo === Build all USB environments ===
echo Firmware output root: %FW_ROOT%
echo Firmware version:   %FW_VER%
echo.

call :build_one wthrbase_USB_BACKUPSERVER PleasantB
call :build_one wthrbase_USB_MainServer PleasantWeather
call :build_one Out32_USB Outside
call :build_one FRBoP_USB FRBoP
call :build_one FamRm_USB FamRm
call :build_one Den_USB Den
call :build_one Office_USB Office
call :build_one MastBR_USB MastBR
call :build_one LivRm_USB LivRm
call :build_one DiningRm_USB Dining
call :build_one UpHall_USB UpHall
call :build_one Garage_USB Garage
call :build_one BsmntHVAC_USB BsmntHVAC
call :build_one Clock480X480_USB SpaceClock

goto :summary

rem Friendly names (first argument only)
:lookup_alias
set "SINGLE_ENV="
set "SINGLE_DEVICE="
if /i "%~1"=="PleasantServer" set "SINGLE_ENV=wthrbase_USB_MainServer" & set "SINGLE_DEVICE=PleasantWeather"
if /i "%~1"=="PleasantWeather" set "SINGLE_ENV=wthrbase_USB_MainServer" & set "SINGLE_DEVICE=PleasantWeather"
if /i "%~1"=="PleasantB" set "SINGLE_ENV=wthrbase_USB_BACKUPSERVER" & set "SINGLE_DEVICE=PleasantB"
if /i "%~1"=="PleasantBackup" set "SINGLE_ENV=wthrbase_USB_BACKUPSERVER" & set "SINGLE_DEVICE=PleasantB"
if /i "%~1"=="PleasantBackupServer" set "SINGLE_ENV=wthrbase_USB_BACKUPSERVER" & set "SINGLE_DEVICE=PleasantB"
exit /b 0

:lookup_device
set "SINGLE_DEVICE="
if /i "%~1"=="wthrbase_USB_BACKUPSERVER" set "SINGLE_DEVICE=PleasantB"
if /i "%~1"=="wthrbase_USB_MainServer" set "SINGLE_DEVICE=PleasantWeather"
if /i "%~1"=="Out32_USB" set "SINGLE_DEVICE=Outside"
if /i "%~1"=="FRBoP_USB" set "SINGLE_DEVICE=FRBoP"
if /i "%~1"=="FamRm_USB" set "SINGLE_DEVICE=FamRm"
if /i "%~1"=="Den_USB" set "SINGLE_DEVICE=Den"
if /i "%~1"=="Office_USB" set "SINGLE_DEVICE=Office"
if /i "%~1"=="MastBR_USB" set "SINGLE_DEVICE=MastBR"
if /i "%~1"=="LivRm_USB" set "SINGLE_DEVICE=LivRm"
if /i "%~1"=="DiningRm_USB" set "SINGLE_DEVICE=Dining"
if /i "%~1"=="UpHall_USB" set "SINGLE_DEVICE=UpHall"
if /i "%~1"=="Garage_USB" set "SINGLE_DEVICE=Garage"
if /i "%~1"=="BsmntHVAC_USB" set "SINGLE_DEVICE=BsmntHVAC"
if /i "%~1"=="Clock480X480_USB" set "SINGLE_DEVICE=SpaceClock"
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
