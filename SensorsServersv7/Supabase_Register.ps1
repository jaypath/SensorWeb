# Device registration helper for Supabase_Register.bat
# Provisions a MAC for an existing Auth user and prints a 4-char claim code.

param(
    [Parameter(Mandatory = $true)][string]$Email,
    [Parameter(Mandatory = $true)][string]$Mac,
    [Parameter(Mandatory = $true)][string]$DeviceName
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$projectRefPath = Join-Path $root 'supabase\.temp\linked-project.json'

function Write-Fail {
    param([string]$ErrorCode, [string]$Detail)
    Write-Output 'FAILURE'
    Write-Output "  error: $ErrorCode"
    if ($Detail) { Write-Output "  detail: $Detail" }
    exit 1
}

function Write-Ok {
    param(
        [string]$Owner,
        [string]$MacPretty,
        [string]$MacCanon,
        [string]$Name,
        [string]$Claim,
        [string]$Expires
    )
    Write-Output 'SUCCESS'
    Write-Output "  owner: $Owner"
    Write-Output "  mac: $MacPretty"
    Write-Output "  device_mac: $MacCanon"
    Write-Output "  name: $Name"
    Write-Output "  claim_code: $Claim"
    Write-Output "  expires_at: $Expires"
    Write-Output "  site: home"
}

function Normalize-Email([string]$raw) {
    $e = $raw.Trim().ToLowerInvariant()
    if (-not $e -or -not $e.Contains('@') -or $e.Length -gt 254) { return $null }
    return $e
}

function Normalize-Mac([string]$raw) {
    $m = $raw.Trim()
    if ($m -notmatch '^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$') { return $null }
    return ($m -replace '[^0-9A-Fa-f]', '').ToUpperInvariant()
}

function Format-Mac([string]$canon) {
    $pairs = for ($i = 0; $i -lt 12; $i += 2) { $canon.Substring($i, 2) }
    return ($pairs -join ':').ToLowerInvariant()
}

function Sql-Lit([string]$s) {
    return "'" + ($s -replace "'", "''") + "'"
}

function Get-CliToken {
    $cs = @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public class CredSbReg {
  [DllImport("advapi32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
  public static extern bool CredRead(string target, int type, int reservedFlag, out IntPtr credentialPtr);
  [DllImport("advapi32.dll", SetLastError=true)] public static extern bool CredFree(IntPtr cred);
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
  public struct CREDENTIAL {
    public int Flags; public int Type; public IntPtr TargetName; public IntPtr Comment;
    public System.Runtime.InteropServices.ComTypes.FILETIME LastWritten; public int CredentialBlobSize;
    public IntPtr CredentialBlob; public int Persist; public int AttributeCount; public IntPtr Attributes;
    public IntPtr TargetAlias; public IntPtr UserName;
  }
  public static string ReadUtf8(string target) {
    IntPtr p; if (!CredRead(target, 1, 0, out p)) return null;
    var c = (CREDENTIAL)Marshal.PtrToStructure(p, typeof(CREDENTIAL));
    byte[] bytes = new byte[c.CredentialBlobSize];
    Marshal.Copy(c.CredentialBlob, bytes, 0, c.CredentialBlobSize);
    CredFree(p);
    return Encoding.UTF8.GetString(bytes).TrimEnd('\0');
  }
}
'@
    Add-Type -TypeDefinition $cs
    $token = [CredSbReg]::ReadUtf8('Supabase CLI:supabase')
    if (-not $token) {
        Write-Fail 'cli_not_logged_in' 'No Supabase CLI token in Windows Credential Manager. Run: supabase login'
    }
    return $token
}

function Get-ProjectRef {
    if (-not (Test-Path -LiteralPath $projectRefPath)) {
        Write-Fail 'not_linked' "Missing $projectRefPath - run supabase link in this repo"
    }
    $j = Get-Content -LiteralPath $projectRefPath -Raw | ConvertFrom-Json
    if (-not $j.ref) { Write-Fail 'not_linked' 'linked-project.json has no ref' }
    return [string]$j.ref
}

function Invoke-SbSql {
    param([string]$Sql, [string]$Token, [string]$Ref)
    $body = (@{ query = [string]$Sql } | ConvertTo-Json -Compress -Depth 5)
    $headers = @{ Authorization = "Bearer $Token"; 'Content-Type' = 'application/json' }
    try {
        $resp = Invoke-WebRequest -Method Post `
            -Uri "https://api.supabase.com/v1/projects/$Ref/database/query" `
            -Headers $headers `
            -Body ([Text.Encoding]::UTF8.GetBytes($body)) `
            -UseBasicParsing
        $text = $resp.Content
        if (-not $text) { return @() }
        return ($text | ConvertFrom-Json)
    } catch {
        $msg = $_.Exception.Message
        try {
            $r = $_.Exception.Response
            if ($r) {
                $sr = New-Object System.IO.StreamReader($r.GetResponseStream())
                $msg = $sr.ReadToEnd()
            }
        } catch {}
        Write-Fail 'sql_failed' $msg
    }
}

function Get-ProjectApiKeys {
    param([string]$Token, [string]$Ref)
    try {
        $keys = Invoke-RestMethod -Method Get `
            -Uri "https://api.supabase.com/v1/projects/$Ref/api-keys" `
            -Headers @{ Authorization = "Bearer $Token" }
    } catch {
        Write-Fail 'api_keys_failed' $_.Exception.Message
    }
    $anon = ($keys | Where-Object { $_.name -eq 'anon' }).api_key
    $svc = ($keys | Where-Object { $_.name -eq 'service_role' }).api_key
    if (-not $anon -or -not $svc) { Write-Fail 'api_keys_failed' 'anon or service_role key missing' }
    return @{ Anon = $anon; Service = $svc }
}

function Invoke-SbRest {
    param(
        [string]$Method,
        [string]$Path,
        [string]$ProjectUrl,
        [string]$ServiceKey,
        $Body = $null
    )
    $headers = @{
        apikey         = $ServiceKey
        Authorization  = "Bearer $ServiceKey"
        'Content-Type' = 'application/json'
        Prefer         = 'return=representation'
    }
    $uri = $ProjectUrl.TrimEnd('/') + $Path
    $json = $null
    if ($null -ne $Body) { $json = ($Body | ConvertTo-Json -Compress -Depth 8) }
    try {
        if ($json) {
            return Invoke-RestMethod -Method $Method -Uri $uri -Headers $headers -Body $json
        }
        return Invoke-RestMethod -Method $Method -Uri $uri -Headers $headers
    } catch {
        $msg = $_.Exception.Message
        try {
            if ($_.ErrorDetails.Message) { $msg = $_.ErrorDetails.Message }
        } catch {}
        Write-Fail 'rest_failed' "$Method $Path : $msg"
    }
}

function New-ApiKey {
    $bytes = New-Object byte[] 32
    [System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
    $b64 = [Convert]::ToBase64String($bytes).TrimEnd('=') -replace '\+', '-' -replace '/', '_'
    $key = "sw_$b64"
    return @{ ApiKey = $key; Prefix = $key.Substring(0, 8) }
}

function New-ClaimCode {
    $alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789'
    $bytes = New-Object byte[] 4
    [System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
    $chars = for ($i = 0; $i -lt 4; $i++) { $alphabet[$bytes[$i] % $alphabet.Length] }
    return -join $chars
}

$email = Normalize-Email $Email
if (-not $email) { Write-Fail 'invalid_email' $Email }

$mac = Normalize-Mac $Mac
if (-not $mac) { Write-Fail 'invalid_mac' 'Expected aa:bb:cc:dd:ee:ff' }

$name = $DeviceName.Trim()
if (-not $name) { Write-Fail 'invalid_name' 'Device name is required' }
if ($name.Length -gt 64) { $name = $name.Substring(0, 64) }

$token = Get-CliToken
$ref = Get-ProjectRef
$projectUrl = "https://$ref.supabase.co"
$keys = Get-ProjectApiKeys -Token $token -Ref $ref

$sqlUser = 'select id::text as id, email from auth.users where lower(email) = ' + (Sql-Lit $email) + ' limit 1;'
$userRows = Invoke-SbSql -Token $token -Ref $ref -Sql $sqlUser
if (-not $userRows -or @($userRows).Count -eq 0) {
    Write-Fail 'user_not_found' $email
}
$userId = [string]@($userRows)[0].id
$userEmail = [string]@($userRows)[0].email

$sqlDev = 'select id::text as id, name, is_active from public.devices where device_mac = ' + (Sql-Lit $mac) + ' limit 1;'
$existing = Invoke-SbSql -Token $token -Ref $ref -Sql $sqlDev
if ($existing -and @($existing).Count -gt 0) {
    $exName = @($existing)[0].name
    Write-Fail 'already_registered' ("MAC $mac already exists (name=$exName)")
}

$sqlSite = 'select public.ensure_site(' + (Sql-Lit $userId) + '::uuid, ''home'', ''home'')::text as site_id;'
$siteRows = Invoke-SbSql -Token $token -Ref $ref -Sql $sqlSite
$siteId = [string]@($siteRows)[0].site_id
if (-not $siteId) { Write-Fail 'site_failed' 'ensure_site returned no id' }

$sqlSub = 'insert into public.subscriptions (user_id, plan, status, valid_until, features) select ' + (Sql-Lit $userId) + '::uuid, ''trial'', ''trial'', null, ''{}''::jsonb where not public.user_has_cloud_access(' + (Sql-Lit $userId) + '::uuid);'
[void](Invoke-SbSql -Token $token -Ref $ref -Sql $sqlSub)

$cred = New-ApiKey
$claim = New-ClaimCode
$sqlHash = 'select extensions.crypt(' + (Sql-Lit $cred.ApiKey) + ', extensions.gen_salt(''bf'', 10)) as api_key_hash;'
$hashRows = Invoke-SbSql -Token $token -Ref $ref -Sql $sqlHash
$apiKeyHash = [string]@($hashRows)[0].api_key_hash
if (-not $apiKeyHash) { Write-Fail 'hash_failed' 'pgcrypto crypt() returned empty' }

$expires = [DateTime]::UtcNow.AddDays(180).ToString('yyyy-MM-ddTHH:mm:ssZ')

$created = Invoke-SbRest -Method POST -Path '/rest/v1/devices' -ProjectUrl $projectUrl -ServiceKey $keys.Service -Body @{
    user_id        = $userId
    device_mac     = $mac
    api_key_hash   = $apiKeyHash
    api_key_prefix = $cred.Prefix
    name           = $name
    dev_name       = $name
    is_active      = $true
    site_id        = $siteId
}
if (-not $created) { Write-Fail 'device_insert_failed' 'devices insert returned empty' }

Invoke-SbRest -Method DELETE -Path "/rest/v1/device_provisioning?device_mac=eq.$mac" -ProjectUrl $projectUrl -ServiceKey $keys.Service | Out-Null

$staged = Invoke-SbRest -Method POST -Path '/rest/v1/device_provisioning' -ProjectUrl $projectUrl -ServiceKey $keys.Service -Body @{
    device_mac        = $mac
    user_id           = $userId
    user_email        = $userEmail
    api_key_plaintext = $cred.ApiKey
    claim_code        = $claim
    expires_at        = $expires
    created_by        = $userId
    project_url       = $projectUrl
    anon_key          = $keys.Anon
    mint_path         = '/functions/v1/mint-device-jwt'
    device_api_path   = '/functions/v1/device-api'
    claim_path        = '/functions/v1/claim-device'
    api_version       = 1
    site_slug         = 'home'
    site_id           = $siteId
}
if (-not $staged) { Write-Fail 'provision_insert_failed' 'device_provisioning insert returned empty' }

Write-Ok -Owner $userEmail -MacPretty (Format-Mac $mac) -MacCanon $mac -Name $name -Claim $claim -Expires $expires
exit 0
