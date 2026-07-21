# Build helper for build_all_usb.bat
# Modes:
#   list    - print non-OTA PlatformIO envs as env|deviceName (one per line)
#   device  - print device name for -EnvName (from _MYDEVICENAME in platformio.ini)
#   version - print CONFIG_APP_PROJECT_VER from platformio.ini

param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('list', 'device', 'version')]
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

function Get-EnvSectionsFromIni {
    param([string]$Path)
    $sections = New-Object System.Collections.Generic.List[object]
    $current = $null

    function Flush {
        if ($null -ne $current) {
            $sections.Add($current) | Out-Null
        }
    }

    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^\[env:([^\]]+)\]') {
            Flush
            $current = [pscustomobject]@{
                Name           = $Matches[1].Trim()
                UploadProtocol = $null
                DeviceName     = $null
            }
            continue
        }
        if ($null -eq $current) { continue }
        if ($line -match '^\s*;') { continue }
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
            Name       = $sec.Name
            DeviceName = $device
            HasExplicitDevice = [bool]$sec.DeviceName
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
            Write-Output ("{0}|{1}" -f $t.Name, $t.DeviceName)
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
}
