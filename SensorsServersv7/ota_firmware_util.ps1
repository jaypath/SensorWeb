# Helper for ota_from_firmware.bat
# Modes:
#   version - print CONFIG_APP_PROJECT_VER from platformio.ini
#   list    - print upload jobs: env|ip|auth|device|binPath|binVersion|why
#            Matches firmware\<DeviceName>-<x.y.z>.bin (or DeviceName.bin)
#            to espota envs by _MYDEVICENAME (resolves ${env:X.build_flags} inheritance).
#
# Optional: -IpPrefix 192.168.68.  -DeviceFilter FamRm  -PreferVersion 9.4.21

param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('version', 'list')]
    [string]$Mode,

    [string]$IniPath = '',
    [string]$FirmwareDir = '',
    [string]$IpPrefix = '',
    [string]$DeviceFilter = '',
    [string]$PreferVersion = ''
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
if (-not $IniPath) { $IniPath = Join-Path $root 'platformio.ini' }
if (-not $FirmwareDir) { $FirmwareDir = Join-Path $root 'firmware' }

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
    $buildFlagRefs = New-Object System.Collections.Generic.List[string]

    function Flush {
        if ($null -eq $current) { return }
        $current.BuildFlagRefs = @($buildFlagRefs)
        $sections.Add($current) | Out-Null
    }

    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^\[env:([^\]]+)\]') {
            Flush
            $current = [pscustomobject]@{
                Name           = $Matches[1].Trim()
                UploadProtocol = $null
                UploadIp       = $null
                UploadAuth     = '12345678'
                DeviceName     = $null
                BuildFlagRefs  = @()
            }
            $buildFlagRefs = New-Object System.Collections.Generic.List[string]
            continue
        }
        if ($null -eq $current) { continue }
        if ($line -match '^\s*;') { continue }
        if ($line -match '^\s*upload_protocol\s*=\s*(\S+)') {
            $current.UploadProtocol = $Matches[1].Trim()
            continue
        }
        if ($line -match '^\s*upload_port\s*=\s*(.+)$') {
            $portLine = $Matches[1].Trim()
            if ($portLine -match '^(\S+)') {
                $current.UploadIp = $Matches[1].Trim()
            }
            if ($portLine -match '--auth=(\S+)') {
                $current.UploadAuth = $Matches[1].Trim()
            }
            continue
        }
        if ($line -match '\$\{env:([^:}]+)\.build_flags\}') {
            $buildFlagRefs.Add($Matches[1].Trim()) | Out-Null
            continue
        }
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

function Resolve-DeviceName {
    param(
        $Section,
        [hashtable]$ByName,
        [int]$Depth = 0
    )
    if ($Depth -gt 8) { return $null }
    if ($Section.DeviceName) { return $Section.DeviceName }
    foreach ($ref in $Section.BuildFlagRefs) {
        if (-not $ByName.ContainsKey($ref)) { continue }
        $resolved = Resolve-DeviceName -Section $ByName[$ref] -ByName $ByName -Depth ($Depth + 1)
        if ($resolved) { return $resolved }
    }
    # Fallback: strip _OTA / _USB suffixes from env name
    $n = $Section.Name
    if ($n -match '(?i)^(.+)_OTA(_.*)?$') { return $Matches[1] }
    if ($n -match '(?i)^(.+)_USB$') { return $Matches[1] }
    return $n
}

function Get-OtaTargets {
    param([string]$Path)
    $sections = @(Get-EnvSectionsFromIni -Path $Path)
    $byName = @{}
    foreach ($s in $sections) { $byName[$s.Name] = $s }

    $out = New-Object System.Collections.Generic.List[object]
    foreach ($s in $sections) {
        $isOta = ($s.UploadProtocol -eq 'espota') -or ($s.Name -match '(?i)_OTA')
        if (-not $isOta) { continue }
        if (-not $s.UploadIp) { continue }
        $device = Resolve-DeviceName -Section $s -ByName $byName
        if (-not $device) { continue }
        $out.Add([pscustomobject]@{
            Env        = $s.Name
            Ip         = $s.UploadIp
            Auth       = $s.UploadAuth
            DeviceName = $device
        }) | Out-Null
    }
    return $out
}

function ConvertTo-VersionOrNull {
    param([string]$Text)
    try { return [version]$Text } catch { return $null }
}

function Find-FirmwareForDevice {
    param(
        [string]$Dir,
        [string]$DeviceName,
        [string]$PreferVersion
    )
    if (-not (Test-Path -LiteralPath $Dir)) { return $null }

    $candidates = New-Object System.Collections.Generic.List[object]
    foreach ($f in Get-ChildItem -LiteralPath $Dir -File -Filter '*.bin') {
        $base = [System.IO.Path]::GetFileNameWithoutExtension($f.Name)
        $verText = $null
        $match = $false
        if ($base -ieq $DeviceName) {
            $match = $true
        } elseif ($base -match ('^(?i)' + [regex]::Escape($DeviceName) + '-(.+)$')) {
            $verText = $Matches[1]
            $match = $true
        }
        if (-not $match) { continue }
        $candidates.Add([pscustomobject]@{
            Path    = $f.FullName
            Version = $verText
            Name    = $f.Name
            Mtime   = $f.LastWriteTimeUtc
        }) | Out-Null
    }
    if ($candidates.Count -eq 0) { return $null }

    if ($PreferVersion) {
        $exact = $candidates | Where-Object { $_.Version -eq $PreferVersion } | Select-Object -First 1
        if ($exact) {
            return [pscustomobject]@{ Path = $exact.Path; Version = $exact.Version; Why = "exact $PreferVersion" }
        }
    }

    $withVer = @($candidates | Where-Object { $_.Version } | ForEach-Object {
        $v = ConvertTo-VersionOrNull $_.Version
        if ($null -eq $v) { return }
        [pscustomobject]@{ Item = $_; Ver = $v }
    })
    if ($withVer.Count -gt 0) {
        $best = $withVer | Sort-Object Ver -Descending | Select-Object -First 1
        return [pscustomobject]@{
            Path    = $best.Item.Path
            Version = $best.Item.Version
            Why     = "newest $($best.Item.Version)"
        }
    }

    $newest = $candidates | Sort-Object Mtime -Descending | Select-Object -First 1
    return [pscustomobject]@{
        Path    = $newest.Path
        Version = if ($newest.Version) { $newest.Version } else { 'unknown' }
        Why     = 'newest file (no version in name)'
    }
}

switch ($Mode) {
    'version' {
        Write-Output (Get-FwVersion -Path $IniPath)
    }
    'list' {
        $prefer = $PreferVersion
        if (-not $prefer) {
            try { $prefer = Get-FwVersion -Path $IniPath } catch { $prefer = '' }
        }

        $prefix = $IpPrefix.Trim()
        if ($prefix -and -not $prefix.EndsWith('.')) { $prefix = "$prefix." }

        $filter = $DeviceFilter.Trim()
        $targets = @(Get-OtaTargets -Path $IniPath)
        foreach ($t in $targets) {
            if ($prefix -and -not $t.Ip.StartsWith($prefix)) { continue }
            if ($filter -and ($t.DeviceName -ine $filter) -and ($t.Env -ine $filter)) { continue }

            $fw = Find-FirmwareForDevice -Dir $FirmwareDir -DeviceName $t.DeviceName -PreferVersion $prefer
            if (-not $fw) {
                Write-Output ("SKIP|{0}|{1}|{2}|{3}||no firmware bin for device" -f $t.Env, $t.Ip, $t.Auth, $t.DeviceName)
                continue
            }
            Write-Output ("OTA|{0}|{1}|{2}|{3}|{4}|{5}|{6}" -f `
                $t.Env, $t.Ip, $t.Auth, $t.DeviceName, $fw.Path, $fw.Version, $fw.Why)
        }
    }
}
