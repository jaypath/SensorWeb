@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Register a SensorWeb device in the linked Supabase project (staging claim
rem mailbox). Uses the logged-in Supabase CLI token.
rem
rem Usage:
rem   Supabase_Register.bat owner@email.com aa:bb:cc:dd:ee:ff DeviceName
rem   Supabase_Register.bat owner@email.com aa:bb:cc:dd:ee:ff "Front Shed"
rem
rem On success prints the 4-character claim code. Exit code 0 = success, 1 = fail.

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

set "UTIL=%SCRIPT_DIR%Supabase_Register.ps1"
if not exist "%UTIL%" (
  echo FAILURE
  echo   error: helper_missing
  echo   detail: %UTIL%
  exit /b 1
)

if "%~1"=="" goto :usage
if "%~2"=="" goto :usage
if "%~3"=="" goto :usage

powershell -NoProfile -ExecutionPolicy Bypass -File "%UTIL%" -Email "%~1" -Mac "%~2" -DeviceName "%~3"
exit /b !ERRORLEVEL!

:usage
echo Usage: Supabase_Register.bat ^<owner_email^> ^<aa:bb:cc:dd:ee:ff^> ^<devicename^>
echo Example: Supabase_Register.bat jaypath@gmail.com 5c:46:31:a7:db:cc Shed
exit /b 1
