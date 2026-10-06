@echo off
setlocal EnableExtensions
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "iex ((Get-Content -LiteralPath '%~f0' -Raw) -split '(?m)^goto :powershell\r?\n',2)[1]"
exit /b %ERRORLEVEL%
goto :powershell
$ErrorActionPreference = 'Stop'
$iniPath = Join-Path (Get-Location) 'platformio.ini'
if (-not (Test-Path -LiteralPath $iniPath)) {
  Write-Host "platformio.ini was not found in $(Get-Location)"
  exit 1
}

$raw = [System.IO.File]::ReadAllText($iniPath)
$nl = if ($raw.Contains("`r`n")) { "`r`n" } else { "`n" }
$lines = $raw -split "`r?`n", -1

$sections = New-Object System.Collections.Generic.List[object]
$cur = $null
for ($i = 0; $i -lt $lines.Length; $i++) {
  $line = $lines[$i]
  if ($line -match '^\[env:([^\]]+)\]\s*$') {
    $cur = [pscustomobject]@{
      Name = $Matches[1]; Hub = $false; Device = ''; Inherits = ''
      HeaderLine = $i; PortLine = -1; PortIp = ''; PortRest = ''
    }
    $sections.Add($cur)
    continue
  }
  if ($line -match '^\[') { $cur = $null; continue }
  if (-not $cur) { continue }
  $trim = $line.Trim()
  if ($trim.StartsWith(';')) { continue }
  if ($trim -match '_MYDEVICENAME=\\"([^"\\]+)\\"') { $cur.Device = $Matches[1] }
  if ($trim -match '_IS_SERVER_HUB=1(\s|$)') { $cur.Hub = $true }
  if ($trim -match '\$\{env:([^\.}]+)\.build_flags\}') { $cur.Inherits = $Matches[1] }
  if ($trim -match '^upload_port\s*=\s*([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)(.*)$') {
    $cur.PortLine = $i
    $cur.PortIp = $Matches[1]
    $cur.PortRest = $Matches[2]
  }
}

$byName = @{}
foreach ($s in $sections) { $byName[$s.Name] = $s }
foreach ($s in $sections) {
  $seen = @{}
  $walk = $s
  while ($walk.Inherits -and -not $seen.ContainsKey($walk.Name)) {
    $seen[$walk.Name] = $true
    if (-not $byName.ContainsKey($walk.Inherits)) { break }
    $parent = $byName[$walk.Inherits]
    if (-not $s.Device -and $parent.Device) { $s.Device = $parent.Device }
    if (-not $s.Hub -and $parent.Hub) { $s.Hub = $true }
    $walk = $parent
  }
}

$hub = $sections | Where-Object { $_.Hub } | Select-Object -First 1
if (-not $hub) {
  Write-Host "No hub build was found in platformio.ini."
  exit 1
}
$hubDevice = $hub.Device
$target = $hub
if (-not $target.PortIp -and $hub.Name -match '_USB') {
  $otaName = $hub.Name -replace '_USB', '_OTA'
  if ($byName.ContainsKey($otaName) -and $byName[$otaName].PortIp) { $target = $byName[$otaName] }
}
if (-not $target.PortIp) {
  Write-Host "Hub $($hub.Name) has no OTA IP address in platformio.ini."
  exit 1
}

Write-Host "Hub: $($target.Name)  $($hubDevice)  http://$($target.PortIp)/DEVICES"
try {
  $resp = Invoke-WebRequest -Uri "http://$($target.PortIp)/DEVICES" -UseBasicParsing -TimeoutSec 20
} catch {
  Write-Host "The hub did not answer: $($_.Exception.Message)"
  exit 1
}

function Parse-CsvRow([string]$row) {
  $fields = New-Object System.Collections.Generic.List[string]
  $curField = New-Object System.Text.StringBuilder
  $inQ = $false
  for ($c = 0; $c -lt $row.Length; $c++) {
    $ch = $row[$c]
    if ($inQ) {
      if ($ch -eq '"' -and ($c + 1) -lt $row.Length -and $row[$c + 1] -eq '"') {
        [void]$curField.Append('"'); $c++; continue
      }
      if ($ch -eq '"') { $inQ = $false; continue }
      [void]$curField.Append($ch); continue
    }
    if ($ch -eq '"') { $inQ = $true; continue }
    if ($ch -eq ',') { $fields.Add($curField.ToString()); $curField.Clear() | Out-Null; continue }
    [void]$curField.Append($ch)
  }
  $fields.Add($curField.ToString())
  return $fields
}

$fromHub = New-Object System.Collections.Generic.List[object]
foreach ($row in ($resp.Content -split "`r?`n")) {
  if ([string]::IsNullOrWhiteSpace($row)) { continue }
  $f = Parse-CsvRow $row.Trim()
  if ($f.Count -lt 4) { continue }
  $fromHub.Add([pscustomobject]@{ Name = $f[0]; Ip = $f[1]; Status = $f[2]; When = $f[3] })
}
if ($fromHub.Count -eq 0) {
  Write-Host "The hub returned no device rows."
  exit 1
}
$hubByName = @{}
foreach ($d in $fromHub) { if (-not $hubByName.ContainsKey($d.Name)) { $hubByName[$d.Name] = $d } }

function Get-AutomationFlagLines([int]$headerLine) {
  $found = New-Object System.Collections.Generic.List[int]
  if ($headerLine -gt 0 -and $lines[$headerLine - 1] -match '^\s*;\s*not for automation\s*$') {
    $found.Add($headerLine - 1)
  }
  for ($j = $headerLine + 1; $j -lt $lines.Length; $j++) {
    if ($lines[$j] -match '^\[') { break }
    if ($lines[$j] -match '^\s*;\s*not for automation\s*$') { $found.Add($j) }
  }
  return $found
}

$otaEnvs = @($sections | Where-Object { $_.Name -match '_OTA' -and $_.Name -ne $target.Name })
$updated = New-Object System.Collections.Generic.List[string]
$unchanged = New-Object System.Collections.Generic.List[string]
$excluded = New-Object System.Collections.Generic.List[string]
$matchedNames = New-Object System.Collections.Generic.HashSet[string]
$deleteLines = New-Object System.Collections.Generic.List[int]
$insertAfter = New-Object System.Collections.Generic.List[int]
$changed = $false

foreach ($env in $otaEnvs) {
  if (-not $env.Device -or $env.Device -eq $hubDevice) { continue }
  $row = $null
  $registered = $false
  if ($hubByName.ContainsKey($env.Device)) {
    $row = $hubByName[$env.Device]
    [void]$matchedNames.Add($env.Device)
    $ipOk = $row.Ip -match '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$' -and $row.Ip -ne '0.0.0.0'
    $registered = ($row.Status -eq 'ok' -and $ipOk)
  }

  if ($registered) {
    $flags = @(Get-AutomationFlagLines $env.HeaderLine)
    foreach ($n in $flags) { if (-not $deleteLines.Contains($n)) { $deleteLines.Add($n) } }
    $ipNote = "$($env.PortIp)  ok"
    if ($env.PortLine -ge 0 -and $env.PortIp -ne $row.Ip) {
      $lines[$env.PortLine] = "upload_port = $($row.Ip)$($env.PortRest)"
      $changed = $true
      $ipNote = "$($env.PortIp) -> $($row.Ip)  ok"
    }
    if ($flags.Count -gt 0) {
      $changed = $true
      $updated.Add("$($env.Name)  $($env.Device)  $ipNote  flag removed")
    } elseif ($env.PortLine -ge 0 -and $env.PortIp -ne $row.Ip) {
      $updated.Add("$($env.Name)  $($env.Device)  $ipNote")
    } else {
      $unchanged.Add("$($env.Name)  $($env.Device)  $($env.PortIp)  ok")
    }
    continue
  }

  $reason = if ($row) { $row.Status } else { 'not on hub' }
  $flags = @(Get-AutomationFlagLines $env.HeaderLine)
  if ($flags.Count -eq 0) {
    if (-not $insertAfter.Contains($env.HeaderLine)) { $insertAfter.Add($env.HeaderLine) }
    $changed = $true
    $excluded.Add("$($env.Name)  $($env.Device)  $reason  flag added")
  } else {
    $excluded.Add("$($env.Name)  $($env.Device)  $reason  already marked")
  }
}

if ($changed) {
  $work = New-Object System.Collections.Generic.List[string]
  foreach ($ln in $lines) { $work.Add([string]$ln) }
  $ops = New-Object System.Collections.Generic.List[object]
  foreach ($n in $deleteLines) { $ops.Add([pscustomobject]@{ Kind = 'del'; At = $n }) }
  foreach ($n in $insertAfter) { $ops.Add([pscustomobject]@{ Kind = 'ins'; At = $n }) }
  foreach ($op in ($ops | Sort-Object At -Descending)) {
    if ($op.Kind -eq 'del') { $work.RemoveAt($op.At) }
    else { $work.Insert($op.At + 1, ';not for automation') }
  }
  $utf8 = New-Object System.Text.UTF8Encoding $false
  [System.IO.File]::WriteAllText($iniPath, ($work -join $nl), $utf8)
}

$unknownHub = New-Object System.Collections.Generic.List[string]
foreach ($d in $fromHub) {
  if ($d.Name -eq $hubDevice) { continue }
  if (-not $matchedNames.Contains($d.Name)) { $unknownHub.Add("$($d.Name)  $($d.Ip)  $($d.Status)") }
}
$unknownIni = New-Object System.Collections.Generic.List[string]
foreach ($env in $otaEnvs) {
  if (-not $env.Device -or $env.Device -eq $hubDevice) { continue }
  if (-not $hubByName.ContainsKey($env.Device)) { $unknownIni.Add("$($env.Name)  $($env.Device)") }
}

Write-Host ""
Write-Host "Updated:"
if ($updated.Count -eq 0) { Write-Host "  (none)" } else { $updated | ForEach-Object { Write-Host "  $_" } }
Write-Host ""
Write-Host "Not updated:"
if ($unchanged.Count -eq 0) { Write-Host "  (none)" } else { $unchanged | ForEach-Object { Write-Host "  $_" } }
Write-Host ""
Write-Host "Not for automation:"
if ($excluded.Count -eq 0) { Write-Host "  (none)" } else { $excluded | ForEach-Object { Write-Host "  $_" } }
Write-Host ""
Write-Host "Unknown device names:"
if ($unknownHub.Count -eq 0 -and $unknownIni.Count -eq 0) { Write-Host "  (none)" }
if ($unknownHub.Count -gt 0) {
  Write-Host "  From the hub, no matching OTA build:"
  $unknownHub | ForEach-Object { Write-Host "    $_" }
}
if ($unknownIni.Count -gt 0) {
  Write-Host "  In platformio.ini, not returned by the hub:"
  $unknownIni | ForEach-Object { Write-Host "    $_" }
}
exit 0
