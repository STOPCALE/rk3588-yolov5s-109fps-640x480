# =============================================================================
#  tools\set-board-ip.ps1
#
#  Change the developer board address in ~/.ssh/config with one command.
#
#  Usage:
#      .\tools\set-board-ip.ps1                     # show current address
#      .\tools\set-board-ip.ps1 192.168.3.231       # set a new IPv4 address
#      .\tools\set-board-ip.ps1 orangepi5.local     # or an mDNS hostname
#      .\tools\set-board-ip.ps1 192.168.3.231 -Alias board
#
#  Files touched:
#      %USERPROFILE%\.ssh\config       (modified in place)
#      %USERPROFILE%\.ssh\config.bak   (backup, overwritten on each run)
#
#  NOTE: this script is intentionally ASCII-only. Windows PowerShell 5.1 reads
#        .ps1 files as ANSI/GBK when there is no UTF-8 BOM, which garbles
#        non-ASCII text and can break parsing. The ssh config file itself is
#        read/written as UTF-8 (no BOM), so it may contain non-ASCII text.
# =============================================================================

param(
    [Parameter(Position = 0)]
    [string] $NewHost,

    [string] $Alias   = "board",
    [string] $CfgPath = "$env:USERPROFILE\.ssh\config"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $CfgPath)) {
    Write-Host "[FAIL] ssh config not found: $CfgPath" -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------- read (UTF-8)
$lines = @(Get-Content -Path $CfgPath -Encoding UTF8)

# ------------------------------------------------- find "Host <alias>" block
$inBlock = $false
$idx     = -1
$curHost = ""

for ($i = 0; $i -lt $lines.Count; $i++) {
    $line = $lines[$i]

    if ($line -match '^\s*Host\s+(.+?)\s*$') {
        $names   = $Matches[1].Trim() -split '\s+'
        $inBlock = ($names -contains $Alias)
        continue
    }

    if ($inBlock -and $line -match '^\s*HostName\s+(\S+)\s*$') {
        $curHost = $Matches[1]
        $idx     = $i
        break
    }
}

if ($idx -lt 0) {
    Write-Host "[FAIL] no 'Host $Alias' block with a HostName line in $CfgPath" -ForegroundColor Red
    exit 1
}

# ------------------------------------------------------- no argument: just show
if ([string]::IsNullOrWhiteSpace($NewHost)) {
    Write-Host "$Alias  ->  $curHost" -ForegroundColor Cyan
    Write-Host "usage: .\tools\set-board-ip.ps1 <new-ip-or-hostname>" -ForegroundColor Gray
    exit 0
}

# ------------------------------------------------------------------- validate
$isIp   = $NewHost -match '^([0-9]{1,3}\.){3}[0-9]{1,3}$'
$isName = $NewHost -match '^[A-Za-z0-9][A-Za-z0-9._-]*$'

if (-not ($isIp -or $isName)) {
    Write-Host "[FAIL] not a valid IPv4 address or hostname: $NewHost" -ForegroundColor Red
    exit 1
}

if ($curHost -eq $NewHost) {
    Write-Host "[SKIP] $Alias is already $NewHost" -ForegroundColor Yellow
    exit 0
}

# ------------------------------------------------- backup + write (UTF-8, no BOM)
Copy-Item $CfgPath "$CfgPath.bak" -Force

$indent = "    "
if ($lines[$idx] -match '^(\s*)') { $indent = $Matches[1] }
$lines[$idx] = $indent + "HostName " + $NewHost

[System.IO.File]::WriteAllLines(
    $CfgPath, $lines, (New-Object System.Text.UTF8Encoding $false))   # $false = no BOM

# ------------------------------------------------------------------ feedback
Write-Host ""
Write-Host "[OK] $Alias : $curHost  ->  $NewHost" -ForegroundColor Green
Write-Host "     backup : $CfgPath.bak"          -ForegroundColor Gray
Write-Host "     verify : ssh $Alias `"echo OK`"" -ForegroundColor Cyan
Write-Host ""
Write-Host "  hint: if the IP keeps changing, ask your router for a DHCP"  -ForegroundColor Yellow
Write-Host "        reservation, or use an mDNS name such as orangepi5.local" -ForegroundColor Yellow
