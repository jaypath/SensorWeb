@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Build (and optionally upload) PlatformIO environments from platformio.ini.
rem
rem Usage:
rem   build_firmware.bat                  Refresh IPs from the hub, then build every OTA (espota) env not marked
rem                                       ;not for automation, skipping envs whose ota_record.txt entry is already
rem                                       at CONFIG_APP_PROJECT_VER
rem   build_firmware.bat -u               Same, and OTA-upload each built env to its upload_port.
rem                                       If an OTA upload fails, the image is sent to wthrlite_OTA and
rem                                       wthrbase_OTA (unless marked ;not for automation) for hub dispersal.
rem   build_firmware.bat -u -n            OTA-upload the existing firmware\ images without compiling
rem   build_firmware.bat Den_OTA -u       Build + OTA-upload Den_OTA to its upload_port (no version check)
rem   build_firmware.bat Den_USB -u -a    Build + USB-upload Den_USB to the auto-detected COM port
rem   build_firmware.bat Den_USB          Build only
rem
rem Flags (any order):  -u / -upload / --upload        upload after building
rem                     -a / -auto / --auto            use the auto-detected serial port (implies -u; single env only)
rem                     -p / -port / --port COMx       upload to this serial port (implies -u; single serial env only)
rem                     COMx or -COMx                  shorthand for -p COMx
rem                     -n / -nocompile / --nocompile  do not build; use the newest firmware\<device>-x.y.z.bin
rem                                                    (error if none, or older than the env's last recorded upload)
rem
rem Env names must match platformio.ini exactly. firmware.bin is copied to firmware\<device>-<version>.bin.
rem Successful uploads are written to ota_record.txt (env|ip_or_port|version|timestamp).
rem Envs using custom_sdkconfig (NimBLE classic ESP32) build with PLATFORMIO_CORE_DIR=%USERPROFILE%\.platformio-nimble.
rem Helper: build_firmware_util.ps1

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

set "PIO=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
if not exist "%PIO%" (
  echo ERROR: PlatformIO not found at %PIO%
  exit /b 1
)
set "UTIL=%SCRIPT_DIR%build_firmware_util.ps1"
if not exist "%UTIL%" (
  echo ERROR: Helper not found: %UTIL%
  exit /b 1
)
set "PS=powershell -NoProfile -ExecutionPolicy Bypass -File"
set "FW_ROOT=%SCRIPT_DIR%firmware"
set "RECORD=%SCRIPT_DIR%ota_record.txt"
set "TARGET_LIST=%TEMP%\build_firmware_targets_%RANDOM%.txt"

set "DO_UPLOAD=0"
set "AUTO_PORT=0"
set "NO_COMPILE=0"
set "ENV_ARG="
set "PORT_ARG="
set "WANT_PORT=0"
:parse_args
if "!WANT_PORT!"=="1" (
  if "%~1"=="" goto :args_done
  set "PORT_ARG=%~1"
  set "WANT_PORT=0"
  shift
  goto :parse_args
)
if "%~1"=="" goto :args_done
set "A=%~1"
set "A_PORT=!A!"
if "!A_PORT:~0,1!"=="-" set "A_PORT=!A_PORT:~1!"
set "A_NUM="
if /i "!A_PORT:~0,3!"=="COM" set "A_NUM=!A_PORT:~3!"
if defined A_NUM (
  set "A_NONNUM="
  for /f "delims=0123456789" %%X in ("!A_NUM!") do set "A_NONNUM=%%X"
  if not defined A_NONNUM (
    set "PORT_ARG=!A_PORT!"
    set "DO_UPLOAD=1"
    shift
    goto :parse_args
  )
)
if /i "!A!"=="-p" (set "WANT_PORT=1" & set "DO_UPLOAD=1" & shift & goto :parse_args)
if /i "!A!"=="-port" (set "WANT_PORT=1" & set "DO_UPLOAD=1" & shift & goto :parse_args)
if /i "!A!"=="--port" (set "WANT_PORT=1" & set "DO_UPLOAD=1" & shift & goto :parse_args)
if /i "!A!"=="-h" goto :help
if /i "!A!"=="-help" goto :help
if /i "!A!"=="--help" goto :help
if "!A!"=="/?" goto :help
if /i "!A!"=="-u" (set "DO_UPLOAD=1") else if /i "!A!"=="-upload" (set "DO_UPLOAD=1") else if /i "!A!"=="--upload" (set "DO_UPLOAD=1") else if /i "!A!"=="-a" (set "AUTO_PORT=1" & set "DO_UPLOAD=1") else if /i "!A!"=="-auto" (set "AUTO_PORT=1" & set "DO_UPLOAD=1") else if /i "!A!"=="--auto" (set "AUTO_PORT=1" & set "DO_UPLOAD=1") else if /i "!A!"=="-n" (set "NO_COMPILE=1") else if /i "!A!"=="-nocompile" (set "NO_COMPILE=1") else if /i "!A!"=="--nocompile" (set "NO_COMPILE=1") else if "!A:~0,1!"=="-" (
  echo ERROR: Unknown flag "!A!"
  exit /b 1
) else if defined ENV_ARG (
  echo ERROR: Only one environment may be given ^("!ENV_ARG!" and "!A!"^)
  exit /b 1
) else (
  set "ENV_ARG=!A!"
)
shift
goto :parse_args
:args_done

if "!AUTO_PORT!"=="1" if not defined ENV_ARG (
  echo ERROR: -auto requires a single environment, e.g. build_firmware.bat Den_USB -u -a
  exit /b 1
)
if "!WANT_PORT!"=="1" (
  echo ERROR: -port requires a value, e.g. build_firmware.bat Den_USB -p COM5
  exit /b 1
)
if defined PORT_ARG (
  if not defined ENV_ARG (
    echo ERROR: -port requires a single environment, e.g. build_firmware.bat Den_USB -p COM5
    exit /b 1
  )
  if "!AUTO_PORT!"=="1" (
    echo ERROR: -port and -auto cannot be used together
    exit /b 1
  )
)

set "FW_VER="
for /f "usebackq delims=" %%V in (`%PS% "%UTIL%" -Mode version`) do set "FW_VER=%%V"
if "!FW_VER!"=="" (
  echo ERROR: Could not read CONFIG_APP_PROJECT_VER from platformio.ini
  exit /b 1
)
if not exist "%FW_ROOT%" mkdir "%FW_ROOT%"

set "OK_COUNT=0"
set "UPLOAD_COUNT=0"
set "FAIL_COUNT=0"
set "SKIP_COUNT=0"
set "EXCLUDE_COUNT=0"
set "QUEUED_COUNT=0"
set "FAILED_LIST="

echo.
echo === build_firmware ===
echo Firmware version: !FW_VER!
echo Output folder:    %FW_ROOT%
if "!DO_UPLOAD!"=="1" (
  if defined PORT_ARG (echo Upload:           yes, port !PORT_ARG!) else if "!AUTO_PORT!"=="1" (echo Upload:           yes, auto-detected port) else (echo Upload:           yes, env upload_port)
) else (
  echo Upload:           no
)
if "!NO_COMPILE!"=="1" (echo Compile:          no, using prebuilt firmware) else (echo Compile:          yes)

if defined ENV_ARG (
  call :process "!ENV_ARG!" "specified"
  goto :summary
)

echo.
echo Refreshing device IPs from the hub...
call "%SCRIPT_DIR%get_IP_from_hub.bat"
if errorlevel 1 (
  echo ERROR: Hub device check failed. No environments were built.
  exit /b 1
)

%PS% "%UTIL%" -Mode list -RecordPath "%RECORD%" > "%TARGET_LIST%"
if errorlevel 1 (
  echo ERROR: Failed to scan platformio.ini
  if exist "%TARGET_LIST%" del "%TARGET_LIST%" >nul 2>&1
  exit /b 1
)
for /f "usebackq tokens=1-3 delims=|" %%A in ("%TARGET_LIST%") do (
  if /i "%%A"=="EXCLUDE" (
    set /a EXCLUDE_COUNT+=1
    echo EXCLUDE: %%B - %%C
  ) else if /i "%%A"=="SKIP" (
    set /a SKIP_COUNT+=1
    echo SKIP:    %%B - %%C
  ) else if /i "%%A"=="BUILD" (
    call :process "%%B" "%%C"
  )
)
if exist "%TARGET_LIST%" del "%TARGET_LIST%" >nul 2>&1
goto :summary

rem ---------------------------------------------------------------------------
:process
set "ENV=%~1"
set "WHY=%~2"
set "STAGED="
set "DEVICE="
set "PROTO="
set "PORT="
set "CORE="
for /f "usebackq tokens=1-4 delims=|" %%A in (`%PS% "%UTIL%" -Mode info -EnvName "%ENV%"`) do (
  set "DEVICE=%%A"
  set "PROTO=%%B"
  set "PORT=%%C"
  set "CORE=%%D"
)
if "!PROTO!"=="-" set "PROTO="
if "!PORT!"=="-" set "PORT="
if "!CORE!"=="-" set "CORE="

echo.
echo ----------------------------------------
echo [!ENV!] !WHY!
echo ----------------------------------------
if not defined DEVICE (
  echo ERROR: Environment "!ENV!" not found in platformio.ini
  goto :process_fail
)

set "PLATFORMIO_CORE_DIR=!CORE!"
if defined PLATFORMIO_CORE_DIR (
  echo PlatformIO core: !PLATFORMIO_CORE_DIR!
  rem IDF component manager fails with Access denied if it has to remove a stale managed_components tree
  if exist "managed_components" (
    attrib -r -s -h "managed_components\*" /s /d >nul 2>&1
    rmdir /s /q "managed_components" >nul 2>&1
  )
)

if "!NO_COMPILE!"=="1" goto :process_prebuilt

"%PIO%" run -e !ENV!
if errorlevel 1 (
  echo ERROR: Build failed for !ENV!
  goto :process_fail
)
set "UP_VER=!FW_VER!"
set "UP_EXTRA="

set "SRC=.pio\build\!ENV!\firmware.bin"
set "DEST_FILE=%FW_ROOT%\!DEVICE!-!FW_VER!.bin"
if not exist "!SRC!" (
  echo ERROR: !SRC! not found after build
  goto :process_fail
)
copy /Y "!SRC!" "!DEST_FILE!" >nul
if errorlevel 1 (
  echo ERROR: Failed to copy firmware to !DEST_FILE!
  goto :process_fail
)
echo OK: firmware copied to !DEST_FILE!
goto :process_upload

:process_prebuilt
set "PB_STATUS="
set "PB_PATH="
set "PB_VER="
set "PB_NOTE="
for /f "usebackq tokens=1-4 delims=|" %%A in (`%PS% "%UTIL%" -Mode prebuilt -EnvName "!ENV!" -RecordPath "%RECORD%" -FirmwareDir "%FW_ROOT%"`) do (
  set "PB_STATUS=%%A"
  set "PB_PATH=%%B"
  set "PB_VER=%%C"
  set "PB_NOTE=%%D"
)
if /i not "!PB_STATUS!"=="OK" (
  if defined PB_PATH (echo ERROR: !PB_PATH!) else (echo ERROR: Could not look up prebuilt firmware for !ENV!)
  goto :process_fail
)
echo Prebuilt: !PB_PATH! ^(v!PB_VER!^)
if not "!PB_NOTE!"=="-" echo WARN: !PB_NOTE!
if not exist ".pio\build\!ENV!" mkdir ".pio\build\!ENV!"
copy /Y "!PB_PATH!" ".pio\build\!ENV!\firmware.bin" >nul
set "STAGED=.pio\build\!ENV!\firmware.bin"
if errorlevel 1 (
  echo ERROR: Failed to stage !PB_PATH! for upload
  goto :process_fail
)
set "UP_VER=!PB_VER!"
set "UP_EXTRA=-t nobuild"

:process_upload
if not "!DO_UPLOAD!"=="1" goto :process_ok

set "UP_PORT_OPT="
if defined PORT_ARG (
  if /i "!PROTO!"=="espota" (
    echo ERROR: -port cannot be used with OTA env !ENV! ^(upload_protocol = espota^)
    goto :process_fail
  )
  set "UP_TARGET=!PORT_ARG!"
  set "UP_PORT_OPT=--upload-port !PORT_ARG!"
) else if "!AUTO_PORT!"=="1" (
  if /i "!PROTO!"=="espota" (
    echo ERROR: -auto cannot be used with OTA env !ENV! ^(upload_protocol = espota^)
    goto :process_fail
  )
  set "UP_TARGET=auto"
) else if defined PORT (
  set "UP_TARGET=!PORT!"
) else if "!NO_COMPILE!"=="1" (
  rem Prebuilt image to a serial env with no upload_port: flash over USB to the auto-detected port
  set "UP_TARGET=auto"
) else (
  echo ERROR: !ENV! has no upload_port in platformio.ini ^(use -a or -p COMx for a serial port^)
  goto :process_fail
)

if /i "!PROTO!"=="espota" (
  echo Uploading !ENV! over OTA to !UP_TARGET!...
  "%PIO%" run -e !ENV! !UP_EXTRA! -t upload
) else if "!NO_COMPILE!"=="1" (
  call :esptool_app_write
) else (
  echo Uploading !ENV! over serial to !UP_TARGET!...
  "%PIO%" run -e !ENV! -t upload !UP_PORT_OPT!
)
if errorlevel 1 (
  echo ERROR: Upload failed for !ENV!
  if /i "!PROTO!"=="espota" (
    call :queue_on_hubs
    if not errorlevel 1 goto :process_queued
  )
  goto :process_fail
)
%PS% "%UTIL%" -Mode record -EnvName "!ENV!" -Port "!UP_TARGET!" -Version "!UP_VER!" -RecordPath "%RECORD%"
if errorlevel 1 echo WARN: Upload succeeded but ota_record.txt was not updated for !ENV!
set /a UPLOAD_COUNT+=1

goto :process_ok

rem Prebuilt serial upload without PlatformIO: the env may never have been built, so there are no
rem bootloader/partition artifacts. Writes only the app to ota_0 plus a blank otadata (boot ota_0);
rem the device keeps its existing bootloader and partition table.
:esptool_app_write
set "FM_STATUS="
set "FM_OTA_OFF="
set "FM_BLANK="
set "FM_APP_OFF="
set "FM_SPEED="
for /f "usebackq tokens=1-5 delims=|" %%A in (`%PS% "%UTIL%" -Mode flashmap -EnvName "!ENV!" -ImagePath "!PB_PATH!"`) do (
  set "FM_STATUS=%%A"
  set "FM_OTA_OFF=%%B"
  set "FM_BLANK=%%C"
  set "FM_APP_OFF=%%D"
  set "FM_SPEED=%%E"
)
if /i not "!FM_STATUS!"=="OK" (
  if defined FM_OTA_OFF (echo ERROR: !FM_OTA_OFF!) else (echo ERROR: Could not read the partition layout for !ENV!)
  exit /b 1
)
set "ET_CORE=!CORE!"
if not defined ET_CORE set "ET_CORE=%USERPROFILE%\.platformio"
set "ET_PY=!ET_CORE!\penv\Scripts\python.exe"
set "ET_TOOL=!ET_CORE!\packages\tool-esptoolpy\esptool.py"
if not exist "!ET_TOOL!" (
  echo ERROR: esptool not found at !ET_TOOL!
  exit /b 1
)
set "ET_PORT="
if /i not "!UP_TARGET!"=="auto" set "ET_PORT=--port !UP_TARGET!"
echo Flashing prebuilt app over serial to !UP_TARGET! ^(ota_0 @ !FM_APP_OFF!, otadata reset @ !FM_OTA_OFF!^)...
"!ET_PY!" "!ET_TOOL!" --chip auto !ET_PORT! --baud !FM_SPEED! write_flash !FM_OTA_OFF! "!FM_BLANK!" !FM_APP_OFF! "!PB_PATH!"
exit /b %errorlevel%

:process_ok
rem A staged prebuilt image must not survive: SCons would treat it as the current build output.
if defined STAGED if exist "!STAGED!" del /q "!STAGED!" >nul 2>&1
set /a OK_COUNT+=1
echo OK: !ENV!
exit /b 0

rem Direct OTA failed. Hand the image to the main hubs so peripherals can pull it later.
rem Does not write ota_record.txt: the device itself has not taken the update yet.
:queue_on_hubs
set "HUB_IMG=!DEST_FILE!"
if "!NO_COMPILE!"=="1" set "HUB_IMG=!PB_PATH!"
set "HUB_NAME=!DEVICE!-!UP_VER!.bin"
if not exist "!HUB_IMG!" (
  echo ERROR: No firmware image to send to the hubs
  exit /b 1
)
set "HUB_OK=0"
echo OTA failed. Sending !HUB_NAME! to the main hubs...
for /f "usebackq tokens=1-3 delims=|" %%A in (`%PS% "%UTIL%" -Mode hubs`) do (
  if /i "%%A"=="SKIP" (
    echo Hub %%B skipped: %%C
  ) else if /i "%%A"=="OK" (
    echo Sending !HUB_NAME! to %%C ^(%%B^)...
    set "HUB_RC="
    for /f "usebackq delims=" %%R in (`%PS% "%UTIL%" -Mode hubput -HubIp "%%B" -ImagePath "!HUB_IMG!" -FirmwareName "!HUB_NAME!"`) do set "HUB_RC=%%R"
    if /i "!HUB_RC!"=="OK" (
      echo OK: %%C accepted !HUB_NAME!
      set "HUB_OK=1"
    ) else if /i "!HUB_RC!"=="INUSE" (
      echo WARN: %%C has !HUB_NAME! in use
    ) else (
      echo ERROR: %%C !HUB_RC!
    )
  ) else (
    echo ERROR: %%A %%B %%C
  )
)
if "!HUB_OK!"=="1" exit /b 0
echo ERROR: Neither main hub accepted !HUB_NAME!
exit /b 1

:process_queued
if defined STAGED if exist "!STAGED!" del /q "!STAGED!" >nul 2>&1
set /a QUEUED_COUNT+=1
echo QUEUED: !ENV! ^(direct OTA failed; firmware is on a hub^)
exit /b 0

:process_fail
if defined STAGED if exist "!STAGED!" del /q "!STAGED!" >nul 2>&1
set /a FAIL_COUNT+=1
set "FAILED_LIST=!FAILED_LIST! !ENV!"
exit /b 1

rem ---------------------------------------------------------------------------
:help
echo.
echo build_firmware.bat - build and optionally upload PlatformIO firmware
echo.
echo Usage:
echo   build_firmware.bat [env] [-u] [-a] [-n] [-help]
echo.
echo   env    A PlatformIO environment name exactly as in platformio.ini, e.g. Den_OTA or Den_USB.
echo          With an env: only that env is processed; ota_record.txt is NOT checked,
echo          and get_IP_from_hub.bat is NOT run. The IP and flags already in
echo          platformio.ini are used.
echo          Without an env: get_IP_from_hub.bat runs first (updates upload_port and
echo          ";not for automation"), then every OTA (espota) env is processed, except
echo          envs marked ";not for automation" and envs whose ota_record.txt entry is
echo          already at the platformio.ini version (these are listed as SKIP).
echo.
echo Flags (any order):
echo   -u, -upload, --upload        Upload after building, to the env's upload_port:
echo                                  espota env  -^> OTA to the IP in upload_port
echo                                  serial env  -^> the COM port in upload_port
echo                                If an espota upload fails, the image is sent over HTTPS to
echo                                wthrlite_OTA and wthrbase_OTA so those hubs can disperse it.
echo                                A hub marked ;not for automation is skipped. The device is
echo                                not recorded as updated until a direct OTA succeeds.
echo                                Error if the env has no upload_port (use -a for serial).
echo   -a, -auto, --auto            Upload to the auto-detected serial port (implies -u).
echo                                Single serial env only, e.g. build_firmware.bat Den_USB -u -a
echo   -p, -port, --port ^<COMx^>     Upload to this serial port (implies -u; overrides upload_port).
echo                                Single serial env only, e.g. build_firmware.bat Den_USB -p COM5
echo                                COMx or -COMx alone is shorthand for -p COMx.
echo   -n, -nocompile, --nocompile  Do not compile; use the newest firmware\^<device^>-x.y.z.bin.
echo                                Error if none exists or it is older than the env's last
echo                                recorded upload. Warns if older than the platformio.ini version.
echo                                With a serial env it flashes over USB (the -p port, the env's
echo                                upload_port, or else the auto-detected port), writing only the app
echo                                (no bootloader/partition table), so the env need not be built first.
echo   -h, -help, --help, /?        Show this help.
echo.
echo Output and records:
echo   - Each build is copied to firmware\^<device^>-^<version^>.bin for later manual upload.
echo   - Each successful upload is written to ota_record.txt as the device's latest firmware.
echo   - A summary of successful, uploaded, skipped, excluded and failed envs is printed at the end;
echo     the exit code is 1 if anything failed.
echo.
echo Examples:
echo   build_firmware.bat                   Build all out-of-date OTA envs
echo   build_firmware.bat -u                Build + OTA-upload all out-of-date OTA envs
echo   build_firmware.bat -u -n             OTA-upload saved images to all out-of-date devices, no compile
echo   build_firmware.bat LivRm_OTA -u      Build + OTA-upload one device, even if already current
echo   build_firmware.bat Den_USB -u -a     Build + flash over USB to the auto-detected COM port
echo   build_firmware.bat Den_USB -p COM5   Build + flash over USB to COM5
echo   build_firmware.bat Office_USB -n COM3  Flash the saved Office image over USB to COM3, no compile
echo   build_firmware.bat Den_USB           Build only
echo.
echo Notes:
echo   - Classic ESP32 (NimBLE) envs build in %%USERPROFILE%%\.platformio-nimble automatically.
echo   - Hub dispersal uses the LMK in %%ARBORYS_LMK%% or in lmk.key next to this script.
echo   - Do not run an IDE build at the same time.
echo.
endlocal & exit /b 0

rem ---------------------------------------------------------------------------
:summary
echo.
echo === Done ===
echo Firmware version: !FW_VER!
echo Successful:       !OK_COUNT!
echo Uploaded:         !UPLOAD_COUNT!
echo Queued on hubs:   !QUEUED_COUNT!
echo Skipped:          !SKIP_COUNT!
echo Excluded:         !EXCLUDE_COUNT!
echo Failed:           !FAIL_COUNT!
if defined FAILED_LIST echo Failed envs:     !FAILED_LIST!
echo.
if !FAIL_COUNT! gtr 0 (
  endlocal & exit /b 1
)
endlocal & exit /b 0
