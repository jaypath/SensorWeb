# Reads CONFIG_APP_PROJECT_VER from platformio.ini (first non-comment match).
$ini = Join-Path $PSScriptRoot 'platformio.ini'
if (-not (Test-Path $ini)) { exit 1 }
foreach ($line in Get-Content $ini) {
    if ($line -match '^\s*;') { continue }
    if ($line -match 'CONFIG_APP_PROJECT_VER' -and $line -match '(\d+\.\d+\.\d+)') {
        Write-Output $Matches[1]
        exit 0
    }
}
exit 1
