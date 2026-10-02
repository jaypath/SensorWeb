# Build helper for build_all_usb.bat
# Modes:
#   list    - print non-OTA PlatformIO envs (BUILD|env|device, or EXCLUDE|... for ;not for automation)
#   device  - print device name for -EnvName (from _MYDEVICENAME in platformio.ini)
#   version - print CONFIG_APP_PROJECT_VER from platformio.ini
#   coredir - print PLATFORMIO_CORE_DIR for -EnvName (NimBLE core if the env sets custom_sdkconfig, else empty)

param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('list', 'device', 'version', 'coredir')]
    [string]$Mode,

    [string]$EnvName = '',
    [string]$IniPath = ''
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
if (-not $IniPath) { $IniPath = Join-Path $root 'platformio.ini' }

function Get-FwVersion {
    param([string]$Path)
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^\s*;') { continue }
        if ($line -match 'CONFIG_APP_PROJECT_VER' -and $line -match '(\d+\.\d+\.\d+)') {
            return $Matches[1]
        }
    }
    throw "Could not read CONFIG_APP_PROJECT_VER from $Path"
}

# HybridCompile envs (custom_sdkconfig) get their own PlatformIO core so their rebuilt IDF libs
# never replace the stock libs used by other envs (no framework reinstall / lib rebuild on switch).
function Get-CoreDirForEnv {
    param([string]$Path, [string]$Env)
    $inEnv = $false
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^\s*\[([^\]]+)\]') {
            $inEnv = ($Matches[1].Trim() -eq "env:$Env")
            continue
        }
        if ($inEnv -and $line -match '^\s*custom_sdkconfig\s*=') {
            return (Join-Path $env:USERPROFILE '.platformio-nimble')
        }
    }
    return ''
}

function Test-NotForAutomationComment {
    param([string]$Line)
    return [bool]($Line -match '^\s*;\s*not for automation\b')
}

function Get-EnvSectionsFromIni {
    param([string]$Path)
    $sections = New-Object System.Collections.Generic.List[object]
    $current = $null
    $pendingSkip = $false

    function Flush {
        if ($null -ne $current) {
            $sections.Add($current) | Out-Null
        }
    }

    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^\s*$') { continue }
        if (Test-NotForAutomationComment $line) {
            $pendingSkip = $true
            continue
        }
        if ($line -match '^\[env:([^\]]+)\]') {
            Flush
            $current = [pscustomobject]@{
                Name            = $Matches[1].Trim()
                UploadProtocol  = $null
                DeviceName      = $null
                SkipAutomation  = $pendingSkip
            }
            $pendingSkip = $false
            continue
        }
        if ($null -eq $current) { continue }
        if ($line -match '^\s*;') { continue }
        # Marker inside this env (before real keys) applies here, not to the next env.
        if ($pendingSkip) { $current.SkipAutomation = $true }
        $pendingSkip = $false
        if ($line -match '^\s*upload_protocol\s*=\s*(\S+)') {
            $current.UploadProtocol = $Matches[1].Trim()
            continue
        }
        # Active -D _MYDEVICENAME=\"Name\" (ignore commented lines above)
        if ($line -match '_MYDEVICENAME\\?\\"([^\\"]+)\\?"' -or
            $line -match '_MYDEVICENAME=\\"([^\\"]+)\\"' -or
            $line -match '_MYDEVICENAME=\"([^\"]+)\"') {
            $current.DeviceName = $Matches[1].Trim()
            continue
        }
    }
    Flush
    return $sections
}

function Test-IsOtaEnv {
    param($Section)
    if ($Section.Name -match '(?i)_OTA') { return $true }
    if ($Section.UploadProtocol -eq 'espota') { return $true }
    return $false
}

function Get-DeviceNameForEnv {
    param($Section)
    if ($Section.DeviceName) { return $Section.DeviceName }
    # Fallback: strip trailing _USB from env name when _MYDEVICENAME is absent
    $n = $Section.Name
    if ($n -match '(?i)^(.+)_USB$') { return $Matches[1] }
    return $n
}

function Get-UsbBuildTargets {
    param([string]$Path)
    $out = New-Object System.Collections.Generic.List[object]
    foreach ($sec in (Get-EnvSectionsFromIni -Path $Path)) {
        if (Test-IsOtaEnv -Section $sec) { continue }
        $device = Get-DeviceNameForEnv -Section $sec
        $out.Add([pscustomobject]@{
            Name              = $sec.Name
            DeviceName        = $device
            HasExplicitDevice = [bool]$sec.DeviceName
            SkipAutomation    = [bool]$sec.SkipAutomation
        }) | Out-Null
    }
    return $out
}

switch ($Mode) {
    'version' {
        Write-Output (Get-FwVersion -Path $IniPath)
    }
    'list' {
        foreach ($t in (Get-UsbBuildTargets -Path $IniPath)) {
            if ($t.SkipAutomation) {
                Write-Output ("EXCLUDE|{0}|{1}|not for automation" -f $t.Name, $t.DeviceName)
            } else {
                Write-Output ("BUILD|{0}|{1}" -f $t.Name, $t.DeviceName)
            }
        }
    }
    'device' {
        if (-not $EnvName) { throw 'EnvName is required for device mode' }
        $hit = (Get-UsbBuildTargets -Path $IniPath) | Where-Object { $_.Name -eq $EnvName } | Select-Object -First 1
        if (-not $hit) {
            # Also allow looking up an OTA env's device name for single builds that pass USB name only
            $all = Get-EnvSectionsFromIni -Path $IniPath
            $sec = $all | Where-Object { $_.Name -eq $EnvName } | Select-Object -First 1
            if (-not $sec) { throw "Environment not found in platformio.ini: $EnvName" }
            Write-Output (Get-DeviceNameForEnv -Section $sec)
        } else {
            Write-Output $hit.DeviceName
        }
    }
    'coredir' {
        if (-not $EnvName) { throw 'EnvName is required for coredir mode' }
        Write-Output (Get-CoreDirForEnv -Path $IniPath -Env $EnvName)
    }
}
