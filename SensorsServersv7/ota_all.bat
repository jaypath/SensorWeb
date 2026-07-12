@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Sequentially OTA-upload every PlatformIO espota environment whose upload_port
rem IP starts with the given prefix, skipping devices already recorded at the
rem current CONFIG_APP_PROJECT_VER (or newer).
rem
rem Usage:
rem   ota_all.bat 192.168.1.
rem   ota_all.bat 192.168.68.
rem   ota_all.bat                  (prompts for prefix)
rem
rem Record file: ota_record.txt  (env|ip|version|timestamp)
rem Helper:      ota_util.ps1

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

set "PIO=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
if not exist "%PIO%" (
  echo ERROR: PlatformIO not found at %PIO%
  exit /b 1
)

set "UTIL=%SCRIPT_DIR%ota_util.ps1"
set "RECORD=%SCRIPT_DIR%ota_record.txt"
set "TARGET_LIST=%TEMP%\ota_all_targets_%RANDOM%.txt"

set "IP_PREFIX=%~1"
if "!IP_PREFIX!"=="" (
  set /p IP_PREFIX=Enter IP prefix ^(e.g. 192.168.1.^): 
)
if "!IP_PREFIX!"=="" (
  echo ERROR: IP prefix is required.
  exit /b 1
)

if not exist "%RECORD%" (
  >"%RECORD%" echo # OTA success record - managed by ota_all.bat
  >>"%RECORD%" echo # Format: env_name^|ip^|version^|yyyy-mm-dd HH:MM:SS
  >>"%RECORD%" echo # One line per PlatformIO OTA environment. Do not edit while ota_all.bat is running.
)

set "FW_VER="
set "OK_COUNT=0"
set "FAIL_COUNT=0"
set "SKIP_COUNT=0"
set "MATCH_COUNT=0"

for /f "usebackq delims=" %%V in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%UTIL%" -Mode version`) do set "FW_VER=%%V"
if "!FW_VER!"=="" (
  echo ERROR: Could not read CONFIG_APP_PROJECT_VER from platformio.ini
  exit /b 1
)

echo.
echo === OTA all matching environments ===
echo IP prefix:  !IP_PREFIX!
echo Firmware:   !FW_VER!
echo Record:     %RECORD%
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%UTIL%" -Mode targets -IpPrefix "!IP_PREFIX!" -RecordPath "%RECORD%" > "%TARGET_LIST%"
if errorlevel 1 (
  echo ERROR: Failed to scan platformio.ini for OTA targets.
  if exist "%TARGET_LIST%" del "%TARGET_LIST%" >nul 2>&1
  exit /b 1
)

for /f "usebackq tokens=1-4 delims=|" %%A in ("%TARGET_LIST%") do (
  if /i "%%A"=="SKIP" (
    set /a SKIP_COUNT+=1
    set /a MATCH_COUNT+=1
    echo SKIP: %%B ^(%%C^) — %%D
  ) else if /i "%%A"=="OTA" (
    set /a MATCH_COUNT+=1
    call :do_ota "%%B" "%%C" "%%D"
  )
)

if exist "%TARGET_LIST%" del "%TARGET_LIST%" >nul 2>&1

echo.
echo === Done ===
echo Firmware version: !FW_VER!
echo Matched prefix:   !MATCH_COUNT!
echo Skipped ^(current^): !SKIP_COUNT!
echo Successful:       !OK_COUNT!
echo Failed:           !FAIL_COUNT!
echo.

if !FAIL_COUNT! gtr 0 (
  endlocal & exit /b 1
)
endlocal & exit /b 0

:do_ota
set "ENV=%~1"
set "IP=%~2"
set "WHY=%~3"
if "!ENV!"=="" exit /b 0

echo.
echo ----------------------------------------
echo OTA: !ENV! @ !IP!  ^(!WHY! -^> !FW_VER!^)
echo ----------------------------------------

"%PIO%" run --target upload --environment !ENV!
if errorlevel 1 (
  echo FAIL: !ENV!
  set /a FAIL_COUNT+=1
  exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%UTIL%" -Mode record -EnvName "!ENV!" -Ip "!IP!" -Version "!FW_VER!" -RecordPath "%RECORD%"
if errorlevel 1 (
  echo WARN: Upload succeeded but failed to update record for !ENV!
  set /a FAIL_COUNT+=1
  exit /b 1
)

echo OK: !ENV! recorded as !FW_VER!
set /a OK_COUNT+=1
exit /b 0
