# Helper for build_firmware.bat
# Modes:
#   version - print CONFIG_APP_PROJECT_VER from platformio.ini
#   list     - OTA (espota) envs as BUILD|env|reason, SKIP|env|already x.y.z, or EXCLUDE|env|not for automation
#   info     - for -EnvName print device|protocol|port|coredir ('-' for empty fields); exit 1 if env not found
#   prebuilt - newest <FirmwareDir>\<device>-x.y.z.bin for -EnvName as OK|path|version|note or ERR|message
#              (ERR if none exists or it is older than the env's last recorded upload)
#   record   - upsert env|port|version|timestamp into ota_record.txt
#   flashmap - for a direct esptool app-only write of -ImagePath with -EnvName's partition table:
#              OK|otadata_offset|blank_otadata_file|ota_0_offset|upload_speed  or  ERR|message
#   hubs     - wthrlite_OTA and wthrbase_OTA as OK|ip|env, SKIP|env|not for automation, or ERR|env|why
#   hubput   - HTTPS (LMK) upload of -ImagePath to -HubIp as firmware -FirmwareName
#              prints OK, INUSE, or ERR|message. Key: ARBORYS_LMK, else lmk.key beside this script.

param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('version', 'list', 'info', 'prebuilt', 'record', 'flashmap', 'hubs', 'hubput')]
    [string]$Mode,

    [string]$EnvName = '',
    [string]$Port = '',
    [string]$Version = '',
    [string]$RecordPath = '',
    [string]$FirmwareDir = '',
    [string]$ImagePath = '',
    [string]$IniPath = '',
    [string]$HubIp = '',
    [string]$FirmwareName = ''
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
if (-not $IniPath) { $IniPath = Join-Path $root 'platformio.ini' }
if (-not $RecordPath) { $RecordPath = Join-Path $root 'ota_record.txt' }
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

# ;not for automation applies to the env it sits in, or to the next [env:*] when only
# blank/comment lines separate them.
function Get-EnvSections {
    param([string]$Path)
    $list = New-Object System.Collections.Generic.List[object]
    $cur = $null
    $pendingSkip = $false
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^\s*$') { continue }
        if ($line -match '^\s*;\s*not for automation\b') {
            if ($null -ne $cur) { $cur.Skip = $true } else { $pendingSkip = $true }
            continue
        }
        if ($line -match '^\s*\[([^\]]+)\]') {
            $name = $Matches[1].Trim()
            $cur = $null
            if ($name -match '^env:(.+)$') {
                $cur = [pscustomobject]@{
                    Name      = $Matches[1].Trim()
                    Skip      = $pendingSkip
                    Protocol  = ''
                    Port      = ''
                    Device    = ''
                    Nimble    = $false
                    Refs      = New-Object System.Collections.Generic.List[string]
                }
                $list.Add($cur) | Out-Null
            }
            $pendingSkip = $false
            continue
        }
        if ($line -match '^\s*;') { continue }
        $pendingSkip = $false
        if ($null -eq $cur) { continue }
        if ($line -match '^\s*upload_protocol\s*=\s*(\S+)') { $cur.Protocol = $Matches[1]; continue }
        if ($line -match '^\s*upload_port\s*=\s*(\S+)') { $cur.Port = $Matches[1]; continue }
        if ($line -match '^\s*custom_sdkconfig\s*=') { $cur.Nimble = $true }
        if ($line -match '_MYDEVICENAME=\\?"([^\\"]+)\\?"') { $cur.Device = $Matches[1] }
        foreach ($m in [regex]::Matches($line, '\$\{env:([^.}]+)\.')) {
            $cur.Refs.Add($m.Groups[1].Value) | Out-Null
        }
    }
    return $list
}

# OTA envs usually inherit build_flags from their _USB env, so follow ${env:X.*} references.
function Get-DeviceName {
    param($Sec, $All)
    if ($Sec.Device) { return $Sec.Device }
    foreach ($r in $Sec.Refs) {
        $ref = $All | Where-Object { $_.Name -eq $r } | Select-Object -First 1
        if ($ref -and $ref.Device) { return $ref.Device }
    }
    if ($Sec.Name -match '(?i)^(.+?)_(USB|OTA)') { return $Matches[1] }
    return $Sec.Name
}

function Test-UsesNimble {
    param($Sec, $All)
    if ($Sec.Nimble) { return $true }
    foreach ($r in $Sec.Refs) {
        $ref = $All | Where-Object { $_.Name -eq $r } | Select-Object -First 1
        if ($ref -and $ref.Nimble) { return $true }
    }
    return $false
}

function Read-Record {
    param([string]$Path)
    $map = @{}
    if (-not (Test-Path -LiteralPath $Path)) { return $map }
    foreach ($line in Get-Content -LiteralPath $Path) {
        $t = $line.Trim()
        if (-not $t -or $t.StartsWith('#')) { continue }
        $p = $t.Split('|')
        if ($p.Count -ge 3) { $map[$p[0].Trim()] = $p[2].Trim() }
    }
    return $map
}

function Test-IsCurrent {
    param([string]$Prev, [string]$Cur)
    if (-not $Prev) { return $false }
    try { return ([version]$Prev) -ge ([version]$Cur) } catch { return $false }
}

function Dash([string]$s) { if ($s) { $s } else { '-' } }

# First-line value of every key in every section ('env' is the shared base section).
function Get-IniKeys {
    param([string]$Path)
    $map = @{}
    $cur = $null
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^\s*[;#]' -or $line -match '^\s*$') { continue }
        if ($line -match '^\s*\[([^\]]+)\]') {
            $cur = $Matches[1].Trim()
            if (-not $map.ContainsKey($cur)) { $map[$cur] = @{} }
            continue
        }
        if ($null -eq $cur) { continue }
        if ($line -match '^([A-Za-z0-9_.]+)\s*=\s*(.*)$') {
            $v = ($Matches[2] -replace '\s;.*$', '').Trim()
            $map[$cur][$Matches[1]] = $v
        }
    }
    return $map
}

# Resolve a key for env:<Env>: own value (following ${section.key}), then ${env:X.*} references, then [env].
function Resolve-EnvKey {
    param($Keys, $Sec, [string]$Key, [int]$Depth = 0)
    if ($Depth -gt 5) { return '' }
    $own = $Keys["env:$($Sec.Name)"]
    if ($own -and $own.ContainsKey($Key) -and $own[$Key]) {
        $v = $own[$Key]
        if ($v -match '^\$\{([^.}]+)\.([^}]+)\}$') {
            $refSec = $Keys[$Matches[1]]
            if ($refSec -and $refSec.ContainsKey($Matches[2])) { return $refSec[$Matches[2]] }
            return ''
        }
        return $v
    }
    foreach ($r in $Sec.Refs) {
        $rs = $Keys["env:$r"]
        if ($rs -and $rs.ContainsKey($Key) -and $rs[$Key]) { return $rs[$Key] }
    }
    if ($Keys.ContainsKey('env') -and $Keys['env'].ContainsKey($Key)) { return $Keys['env'][$Key] }
    return ''
}

function Get-LmkKeyBytes {
    $text = $env:ARBORYS_LMK
    if (-not $text) {
        $keyFile = Join-Path $root 'lmk.key'
        if (Test-Path -LiteralPath $keyFile) {
            $text = (Get-Content -LiteralPath $keyFile -TotalCount 1).Trim()
        }
    }
    if (-not $text) {
        throw 'LMK key not found. Set ARBORYS_LMK or put the 16-character key in lmk.key next to build_firmware.bat.'
    }
    $raw = [Text.Encoding]::ASCII.GetBytes($text)
    $key = New-Object byte[] 16
    $n = [Math]::Min(16, $raw.Length)
    if ($n -gt 0) { [Array]::Copy($raw, $key, $n) }
    return $key
}

# Same framing as BootSecure::encrypt: [u16 le plain len][payload], zero pad to 16, AES-128-CBC, IV prepended.
function Protect-LmkPayload {
    param([byte[]]$Payload, [byte[]]$Key)
    $framed = New-Object byte[] (2 + $Payload.Length)
    $framed[0] = [byte]($Payload.Length -band 0xFF)
    $framed[1] = [byte](($Payload.Length -shr 8) -band 0xFF)
    [Array]::Copy($Payload, 0, $framed, 2, $Payload.Length)
    $pad = 0
    if (($framed.Length % 16) -ne 0) { $pad = 16 - ($framed.Length % 16) }
    $padded = New-Object byte[] ($framed.Length + $pad)
    [Array]::Copy($framed, $padded, $framed.Length)
    $iv = New-Object byte[] 16
    $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
    try { $rng.GetBytes($iv) } finally { $rng.Dispose() }
    $aes = [Security.Cryptography.Aes]::Create()
    try {
        $aes.Mode = [Security.Cryptography.CipherMode]::CBC
        $aes.Padding = [Security.Cryptography.PaddingMode]::None
        $aes.Key = $Key
        $aes.IV = $iv
        $enc = $aes.CreateEncryptor().TransformFinalBlock($padded, 0, $padded.Length)
    } finally { $aes.Dispose() }
    $out = New-Object byte[] (16 + $enc.Length)
    [Array]::Copy($iv, $out, 16)
    [Array]::Copy($enc, 0, $out, 16, $enc.Length)
    return $out
}

function Unprotect-LmkPayload {
    param([byte[]]$Blob, [byte[]]$Key)
    if ($Blob.Length -lt 32 -or (($Blob.Length - 16) % 16) -ne 0) { throw 'Bad cipher length' }
    $iv = New-Object byte[] 16
    [Array]::Copy($Blob, 0, $iv, 0, 16)
    $ct = New-Object byte[] ($Blob.Length - 16)
    [Array]::Copy($Blob, 16, $ct, 0, $ct.Length)
    $aes = [Security.Cryptography.Aes]::Create()
    try {
        $aes.Mode = [Security.Cryptography.CipherMode]::CBC
        $aes.Padding = [Security.Cryptography.PaddingMode]::None
        $aes.Key = $Key
        $aes.IV = $iv
        $plain = $aes.CreateDecryptor().TransformFinalBlock($ct, 0, $ct.Length)
    } finally { $aes.Dispose() }
    $n = [int]$plain[0] + ([int]$plain[1] -shl 8)
    if ($n -le 0 -or (2 + $n) -gt $plain.Length) { throw 'Bad payload length' }
    return [Text.Encoding]::ASCII.GetString($plain, 2, $n)
}

function Send-HttpBody {
    param([string]$Url, [byte[]]$Body, [int]$TimeoutMs)
    [System.Net.ServicePointManager]::Expect100Continue = $false
    $req = [System.Net.HttpWebRequest]::Create($Url)
    $req.Method = 'POST'
    $req.ContentType = 'application/octet-stream'
    $req.Timeout = $TimeoutMs
    $req.ReadWriteTimeout = $TimeoutMs
    $req.KeepAlive = $false
    $req.ContentLength = $Body.Length
    $stream = $req.GetRequestStream()
    try { $stream.Write($Body, 0, $Body.Length) } finally { $stream.Close() }
    $code = 0
    $resp = $null
    try {
        $resp = $req.GetResponse()
        $code = [int]$resp.StatusCode
    } catch [System.Net.WebException] {
        $resp = $_.Exception.Response
        if (-not $resp) { throw $_.Exception.Message }
        $code = [int]$resp.StatusCode
    }
    $ms = New-Object System.IO.MemoryStream
    try { $resp.GetResponseStream().CopyTo($ms) } finally { $resp.Close() }
    return @{ Code = $code; Body = $ms.ToArray() }
}

switch ($Mode) {
    'version' {
        Write-Output (Get-FwVersion -Path $IniPath)
    }
    'list' {
        $ver = Get-FwVersion -Path $IniPath
        $rec = Read-Record -Path $RecordPath
        foreach ($s in (Get-EnvSections -Path $IniPath)) {
            if ($s.Protocol -ne 'espota') { continue }
            if ($s.Skip) {
                Write-Output ("EXCLUDE|{0}|not for automation" -f $s.Name)
            } elseif ($rec.ContainsKey($s.Name) -and (Test-IsCurrent $rec[$s.Name] $ver)) {
                Write-Output ("SKIP|{0}|already {1}" -f $s.Name, $rec[$s.Name])
            } elseif ($rec.ContainsKey($s.Name)) {
                Write-Output ("BUILD|{0}|upgrade from {1}" -f $s.Name, $rec[$s.Name])
            } else {
                Write-Output ("BUILD|{0}|not in record" -f $s.Name)
            }
        }
    }
    'info' {
        if (-not $EnvName) { throw 'EnvName is required for info mode' }
        $all = Get-EnvSections -Path $IniPath
        $s = $all | Where-Object { $_.Name -eq $EnvName } | Select-Object -First 1
        if (-not $s) {
            [Console]::Error.WriteLine("Environment not found in platformio.ini: $EnvName")
            exit 1
        }
        $core = if (Test-UsesNimble $s $all) { Join-Path $env:USERPROFILE '.platformio-nimble' } else { '' }
        Write-Output ('{0}|{1}|{2}|{3}' -f (Get-DeviceName $s $all), (Dash $s.Protocol), (Dash $s.Port), (Dash $core))
    }
    'prebuilt' {
        if (-not $EnvName) { throw 'EnvName is required for prebuilt mode' }
        $all = Get-EnvSections -Path $IniPath
        $s = $all | Where-Object { $_.Name -eq $EnvName } | Select-Object -First 1
        if (-not $s) { Write-Output "ERR|Environment not found in platformio.ini: $EnvName"; break }
        $device = Get-DeviceName $s $all
        $pattern = '^' + [regex]::Escape($device) + '-(\d+\.\d+\.\d+)\.bin$'
        $best = $null
        $bestVer = $null
        if (Test-Path -LiteralPath $FirmwareDir) {
            foreach ($f in Get-ChildItem -LiteralPath $FirmwareDir -File) {
                if ($f.Name -notmatch $pattern) { continue }
                $v = [version]$Matches[1]
                if ($null -eq $bestVer -or $v -gt $bestVer) { $best = $f; $bestVer = $v }
            }
        }
        if (-not $best) { Write-Output "ERR|No prebuilt firmware $device-x.y.z.bin in $FirmwareDir"; break }
        $rec = Read-Record -Path $RecordPath
        if ($rec.ContainsKey($EnvName)) {
            try {
                if ($bestVer -lt [version]$rec[$EnvName]) {
                    Write-Output ("ERR|Newest prebuilt {0} is older than last recorded upload {1}" -f $best.Name, $rec[$EnvName])
                    break
                }
            } catch { }
        }
        $cur = Get-FwVersion -Path $IniPath
        $note = if ($bestVer -lt [version]$cur) { "older than platformio.ini version $cur" } else { '-' }
        Write-Output ('OK|{0}|{1}|{2}' -f $best.FullName, $bestVer, $note)
    }
    'flashmap' {
        if (-not $EnvName) { throw 'EnvName is required for flashmap mode' }
        $all = Get-EnvSections -Path $IniPath
        $s = $all | Where-Object { $_.Name -eq $EnvName } | Select-Object -First 1
        if (-not $s) { Write-Output "ERR|Environment not found in platformio.ini: $EnvName"; break }
        $keys = Get-IniKeys -Path $IniPath
        $csvName = Resolve-EnvKey $keys $s 'board_build.partitions'
        if (-not $csvName) { Write-Output "ERR|No board_build.partitions for $EnvName"; break }
        $csvPath = Join-Path $root $csvName
        if (-not (Test-Path -LiteralPath $csvPath)) { Write-Output "ERR|Partition table not found: $csvName"; break }
        $ota = $null; $app = $null
        foreach ($line in Get-Content -LiteralPath $csvPath) {
            if ($line -match '^\s*#' -or $line -notmatch ',') { continue }
            $f = $line.Split(',') | ForEach-Object { $_.Trim() }
            if ($f.Count -lt 5) { continue }
            if ($f[1] -eq 'data' -and $f[2] -eq 'ota') { $ota = @{ Off = $f[3]; Size = [Convert]::ToInt32($f[4], 16) } }
            if ($f[1] -eq 'app' -and $f[2] -eq 'ota_0') { $app = @{ Off = $f[3]; Size = [Convert]::ToInt32($f[4], 16) } }
        }
        if (-not $ota -or -not $app) { Write-Output "ERR|$csvName has no otadata/ota_0 partition"; break }
        if ($ImagePath) {
            $len = (Get-Item -LiteralPath $ImagePath).Length
            if ($len -gt $app.Size) {
                Write-Output ("ERR|Image is {0} bytes; ota_0 in {1} is only {2} bytes" -f $len, $csvName, $app.Size)
                break
            }
        }
        $blank = Join-Path $env:TEMP ("otadata_blank_{0}.bin" -f $ota.Size)
        if (-not (Test-Path -LiteralPath $blank) -or (Get-Item -LiteralPath $blank).Length -ne $ota.Size) {
            $bytes = New-Object byte[] $ota.Size
            for ($i = 0; $i -lt $bytes.Length; $i++) { $bytes[$i] = 0xFF }
            [System.IO.File]::WriteAllBytes($blank, $bytes)
        }
        $speed = Resolve-EnvKey $keys $s 'upload_speed'
        if (-not $speed) { $speed = '460800' }
        Write-Output ('OK|{0}|{1}|{2}|{3}' -f $ota.Off, $blank, $app.Off, $speed)
    }
    'record' {
        if (-not $EnvName) { throw 'EnvName is required for record mode' }
        if (-not $Port) { throw 'Port is required for record mode' }
        if (-not $Version) { throw 'Version is required for record mode' }
        $lines = New-Object System.Collections.Generic.List[string]
        $lines.Add('# OTA success record - managed by ota_all.bat / build_firmware.bat') | Out-Null
        $lines.Add('# Format: env_name|ip_or_port|version|yyyy-mm-dd HH:MM:SS') | Out-Null
        $lines.Add('# One line per PlatformIO environment. Do not edit while a build script is running.') | Out-Null
        $newLine = '{0}|{1}|{2}|{3}' -f $EnvName, $Port, $Version, (Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
        $replaced = $false
        if (Test-Path -LiteralPath $RecordPath) {
            foreach ($line in Get-Content -LiteralPath $RecordPath) {
                $t = $line.TrimEnd()
                if (-not $t -or $t.StartsWith('#')) { continue }
                if ($t.Split('|')[0].Trim() -eq $EnvName) {
                    $lines.Add($newLine) | Out-Null
                    $replaced = $true
                } else {
                    $lines.Add($t) | Out-Null
                }
            }
        }
        if (-not $replaced) { $lines.Add($newLine) | Out-Null }
        [System.IO.File]::WriteAllLines($RecordPath, $lines, (New-Object System.Text.UTF8Encoding $false))
        Write-Output "Recorded $EnvName -> $Version ($Port)"
    }
    'hubs' {
        $all = Get-EnvSections -Path $IniPath
        foreach ($name in @('wthrlite_OTA', 'wthrbase_OTA')) {
            $s = $all | Where-Object { $_.Name -eq $name } | Select-Object -First 1
            if (-not $s) {
                Write-Output "ERR|$name|not in platformio.ini"
            } elseif ($s.Skip) {
                Write-Output "SKIP|$name|not for automation"
            } elseif (-not $s.Port) {
                Write-Output "ERR|$name|no upload_port"
            } else {
                Write-Output "OK|$($s.Port)|$name"
            }
        }
    }
    'hubput' {
        try {
            if (-not $HubIp) { throw 'HubIp is required' }
            if (-not $FirmwareName) { throw 'FirmwareName is required' }
            if (-not $ImagePath -or -not (Test-Path -LiteralPath $ImagePath)) { throw "Firmware image not found: $ImagePath" }
            $key = Get-LmkKeyBytes
            $fileBytes = [System.IO.File]::ReadAllBytes($ImagePath)
            if ($fileBytes.Length -le 0 -or $fileBytes.Length -gt 8MB) { throw 'Firmware image size is not valid' }
            $announce = '{"msgType":"FirmwareUpload","name":"' + $FirmwareName + '","size":' + $fileBytes.Length + '}'
            $cipher = Protect-LmkPayload -Payload ([Text.Encoding]::ASCII.GetBytes($announce)) -Key $key
            $check = Send-HttpBody -Url ("http://{0}/POST_ENC" -f $HubIp) -Body $cipher -TimeoutMs 20000
            if ($check.Code -lt 200 -or $check.Code -ge 300) { throw ("hub check HTTP {0}" -f $check.Code) }
            $reply = Unprotect-LmkPayload -Blob $check.Body -Key $key
            if ($reply -match '"error"\s*:\s*"in use"') { Write-Output 'INUSE'; exit 2 }
            if ($reply -notmatch '"ok"\s*:\s*true') { throw ("hub refused: {0}" -f $reply) }

            $chunk = 3968
            $wire = New-Object System.IO.MemoryStream
            for ($off = 0; $off -lt $fileBytes.Length; $off += $chunk) {
                $n = [Math]::Min($chunk, $fileBytes.Length - $off)
                $piece = New-Object byte[] $n
                [Array]::Copy($fileBytes, $off, $piece, 0, $n)
                $frame = Protect-LmkPayload -Payload $piece -Key $key
                $lenb = [byte[]]@(
                    [byte]($frame.Length -band 0xFF),
                    [byte](($frame.Length -shr 8) -band 0xFF)
                )
                $wire.Write($lenb, 0, 2)
                $wire.Write($frame, 0, $frame.Length)
            }
            $eof = [byte[]]@(0, 0)
            $wire.Write($eof, 0, 2)
            $put = Send-HttpBody -Url ("http://{0}/FIRMWARE_PUT" -f $HubIp) -Body $wire.ToArray() -TimeoutMs 180000
            $text = [Text.Encoding]::ASCII.GetString($put.Body)
            if ($text -match '"error"\s*:\s*"in use"') { Write-Output 'INUSE'; exit 2 }
            if ($put.Code -ge 200 -and $put.Code -lt 300 -and $text -match '"ok"\s*:\s*true') {
                Write-Output 'OK'
                exit 0
            }
            throw ("hub write failed: {0} {1}" -f $put.Code, $text)
        } catch {
            Write-Output ("ERR|{0}" -f $_.Exception.Message)
            exit 1
        }
    }
}
