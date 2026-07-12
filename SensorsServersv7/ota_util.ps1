# OTA helper for ota_all.bat
# Modes:
#   targets  - list OTA envs matching IP prefix that need an update (env|ip)
#   record   - upsert a successful OTA into the record file
#   version  - print CONFIG_APP_PROJECT_VER from platformio.ini

param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('targets', 'record', 'version')]
    [string]$Mode,

    [string]$IpPrefix = '',
    [string]$EnvName = '',
    [string]$Ip = '',
    [string]$Version = '',
    [string]$RecordPath = '',
    [string]$IniPath = ''
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
if (-not $IniPath) { $IniPath = Join-Path $root 'platformio.ini' }
if (-not $RecordPath) { $RecordPath = Join-Path $root 'ota_record.txt' }

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

function Normalize-IpPrefix {
    param([string]$Prefix)
    $p = $Prefix.Trim()
    if (-not $p) { throw 'IP prefix is empty' }
    if (-not $p.EndsWith('.')) { $p = "$p." }
    return $p
}

function Read-OtaRecord {
    param([string]$Path)
    $map = @{}
    if (-not (Test-Path -LiteralPath $Path)) { return $map }
    foreach ($line in Get-Content -LiteralPath $Path) {
        $t = $line.Trim()
        if (-not $t -or $t.StartsWith('#')) { continue }
        $parts = $t.Split('|')
        if ($parts.Count -lt 3) { continue }
        $map[$parts[0].Trim()] = @{
            Ip      = $parts[1].Trim()
            Version = $parts[2].Trim()
            When    = if ($parts.Count -ge 4) { $parts[3].Trim() } else { '' }
        }
    }
    return $map
}

function Test-NeedsUpdate {
    param(
        [string]$CurrentVersion,
        [hashtable]$Record,
        [string]$Env
    )
    if (-not $Record.ContainsKey($Env)) { return $true }
    $prev = $Record[$Env].Version
    if (-not $prev) { return $true }
    try {
        return ([version]$prev) -lt ([version]$CurrentVersion)
    } catch {
        # Unparseable prior version — treat as needing update
        return $true
    }
}

function Get-OtaEnvsFromIni {
    param([string]$Path)
    $envs = New-Object System.Collections.Generic.List[object]
    $currentEnv = $null
    $protocol = $null
    $portIp = $null

    function Flush-Env {
        if ($currentEnv -and $protocol -eq 'espota' -and $portIp) {
            $envs.Add([pscustomobject]@{ Name = $currentEnv; Ip = $portIp }) | Out-Null
        }
    }

    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^\[env:([^\]]+)\]') {
            Flush-Env
            $currentEnv = $Matches[1].Trim()
            $protocol = $null
            $portIp = $null
            continue
        }
        if ($line -match '^\s*;') { continue }
        if ($line -match '^\s*upload_protocol\s*=\s*(\S+)') {
            $protocol = $Matches[1].Trim()
            continue
        }
        if ($line -match '^\s*upload_port\s*=\s*(\S+)') {
            # First token is the IP; ignore --auth=...
            $portIp = $Matches[1].Trim()
            continue
        }
    }
    Flush-Env
    return $envs
}

switch ($Mode) {
    'version' {
        Write-Output (Get-FwVersion -Path $IniPath)
    }
    'targets' {
        $prefix = Normalize-IpPrefix -Prefix $IpPrefix
        $fwVer = Get-FwVersion -Path $IniPath
        $record = Read-OtaRecord -Path $RecordPath
        $all = Get-OtaEnvsFromIni -Path $IniPath
        foreach ($e in $all) {
            if (-not $e.Ip.StartsWith($prefix)) { continue }
            if (-not (Test-NeedsUpdate -CurrentVersion $fwVer -Record $record -Env $e.Name)) {
                $prev = $record[$e.Name].Version
                Write-Output ("SKIP|{0}|{1}|already {2}" -f $e.Name, $e.Ip, $prev)
                continue
            }
            $reason = 'new'
            if ($record.ContainsKey($e.Name)) {
                $reason = "upgrade from $($record[$e.Name].Version)"
            }
            Write-Output ("OTA|{0}|{1}|{2}" -f $e.Name, $e.Ip, $reason)
        }
    }
    'record' {
        if (-not $EnvName) { throw 'EnvName is required for record mode' }
        if (-not $Ip) { throw 'Ip is required for record mode' }
        if (-not $Version) { throw 'Version is required for record mode' }

        $lines = New-Object System.Collections.Generic.List[string]
        $header = @(
            '# OTA success record - managed by ota_all.bat'
            '# Format: env_name|ip|version|yyyy-mm-dd HH:MM:SS'
            '# One line per PlatformIO OTA environment. Do not edit while ota_all.bat is running.'
        )
        foreach ($h in $header) { $lines.Add($h) | Out-Null }

        $stamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
        $newLine = '{0}|{1}|{2}|{3}' -f $EnvName, $Ip, $Version, $stamp
        $replaced = $false

        if (Test-Path -LiteralPath $RecordPath) {
            foreach ($line in Get-Content -LiteralPath $RecordPath) {
                $t = $line.TrimEnd()
                if (-not $t -or $t.StartsWith('#')) { continue }
                $parts = $t.Split('|')
                if ($parts.Count -ge 1 -and $parts[0].Trim() -eq $EnvName) {
                    $lines.Add($newLine) | Out-Null
                    $replaced = $true
                } else {
                    $lines.Add($t) | Out-Null
                }
            }
        }
        if (-not $replaced) {
            $lines.Add($newLine) | Out-Null
        }

        $utf8NoBom = New-Object System.Text.UTF8Encoding $false
        [System.IO.File]::WriteAllLines($RecordPath, $lines, $utf8NoBom)
        Write-Output "Recorded $EnvName -> $Version ($Ip)"
    }
}
