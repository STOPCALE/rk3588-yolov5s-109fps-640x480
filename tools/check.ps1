# =============================================================================
#  tools\check.ps1  --  local syntax check (no build artifacts, no linking)
#
#  Usage:
#      .\tools\check.ps1                       # check every C++ source under src\
#      .\tools\check.ps1 -File src\rkYolov5s.cc
#
#  Why this exists:
#      Catches typos / syntax errors in about 1 second on the PC, instead of
#      burning a ~30-second sync round-trip to the board just to find out that
#      you typed "pirntf" instead of "printf".
#
#  What it does NOT do:
#      * No linking, no binary produced. The real build still runs on the board.
#      * Plain .c files are skipped, because MinGW has no POSIX headers
#        (<termios.h> ...) that the board provides. Those are only compiled
#        on the board.
#
#  NOTE: ASCII-only on purpose. Windows PowerShell 5.1 reads .ps1 files as
#        ANSI/GBK when there is no UTF-8 BOM, which garbles non-ASCII text and
#        can break parsing.
# =============================================================================

param(
    [string] $File
)

$ErrorActionPreference = "Stop"

$ROOT = Split-Path $PSScriptRoot -Parent                                  # ...\new
$GXX  = "E:\mingw64\bin\g++.exe"
$OCV  = Join-Path (Split-Path $ROOT -Parent) "third_party_headers\opencv4"

if (-not (Test-Path $GXX)) {
    Write-Host "[FAIL] g++ not found: $GXX" -ForegroundColor Red
    Write-Host "       edit the GXX variable at the top of this script" -ForegroundColor Gray
    exit 1
}

# -isystem for OpenCV: it is a third-party header set built for another
# platform, so its warnings must not pollute our output.
$incs = @(
    "-I", (Join-Path $ROOT "include"),
    "-I", (Join-Path $ROOT "include\3rdparty\rga\RK3588\include"),
    "-isystem", $OCV
)

# -Werror=return-type turns "control reaches end of non-void function" from a
# harmless-looking warning into a failure. That one bites hard: a non-void
# function falling off the end returns garbage, so callers behave randomly.
$flags = @("-fsyntax-only", "-std=c++14", "-Wall", "-Werror=return-type")

# ------------------------------------------------------------------ pick files
if ($File) {
    $p = Join-Path $ROOT $File
    if (-not (Test-Path $p)) {
        Write-Host "[FAIL] file not found: $p" -ForegroundColor Red
        exit 1
    }
    $targets = @(Get-Item $p)
} else {
    $srcDir = Join-Path $ROOT "src"
    if (-not (Test-Path $srcDir)) {
        Write-Host "[SKIP] src\ does not exist" -ForegroundColor Yellow
        exit 0
    }
    $targets = @(Get-ChildItem $srcDir -Recurse -Include *.cc, *.cpp | Sort-Object Name)
}

if ($targets.Count -eq 0) {
    Write-Host "[SKIP] no .cc/.cpp found under src\" -ForegroundColor Yellow
    exit 0
}

# ---------------------------------------------------------------------- check
$failed = 0
foreach ($t in $targets) {
    Write-Host ("--> " + $t.Name) -ForegroundColor Cyan
    & $GXX @flags @incs $t.FullName
    if ($LASTEXITCODE -ne 0) {
        $failed++
        Write-Host ("    [FAIL] " + $t.Name) -ForegroundColor Red
    }
    else {
        Write-Host ("    [OK]   " + $t.Name) -ForegroundColor Green
    }
}

Write-Host ""
if ($failed -gt 0) {
    Write-Host ("[FAIL] " + $failed + " file(s) have errors. Fix them BEFORE running sync.ps1.") -ForegroundColor Red
    exit 1
}

Write-Host ("[OK] all " + $targets.Count + " file(s) passed syntax check.") -ForegroundColor Green
Write-Host "     next step:  .\tools\sync.ps1 -Message ""...""" -ForegroundColor Gray
