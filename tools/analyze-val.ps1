# =============================================================================
#  tools\analyze-val.ps1  --  用 val 集统计检测精度（纯本地, 不上板子）
#
#  前置: 先用 tools\batch-infer.ps1 跑出 bi_result.txt
#
#  用法:
#      .\tools\analyze-val.ps1 -Result "E:\desk\logs\20260919-1200\bi_result.txt" `
#                              -Images "D:\qq_tenct\...\val\images"
#
#      # 标签目录不在 images 隔壁时, 手动指定
#      .\tools\analyze-val.ps1 -Result <file> -Images <dir> -Labels <dir>
#
#  标签格式 (YOLO txt):   <class> <cx_norm> <cy_norm> <w_norm> <h_norm>   值都是 0~1
#  换算回像素:  cx = cx_norm * W      r = (w_norm*W + h_norm*H) / 4
#               (w/h 是"直径"归一化值, 所以半径要再除 2)
#
#  ---------------------------------------------------------------------------
#  为什么统计量要成对看（B7-4 得出的教训）:
#      中位数 2.7px 看着很美, 均值 10.2px 却难看 —— 因为它俩根本不是一件事。
#      中位数代表"绝大多数帧有多准"; 均值被少数跑飞的帧拉高。
#      只看中位数会漏掉"1% 的帧完全跑飞"这个致命问题; 只看均值会以为模型很烂。
#      所以这个脚本强制同时打印 mean / median / p90 / max 四个数。
#
#  NOTE: 本文件需要保存为 "UTF-8 with BOM" (PowerShell 5.1 才认中文).
# =============================================================================

param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $Result,

    [Parameter(Mandatory = $true, Position = 1)]
    [string] $Images,

    [string] $Labels = ""
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

if (-not (Test-Path $Result)) { Write-Host "[FAIL] 找不到结果文件: $Result" -ForegroundColor Red; exit 1 }
if (-not (Test-Path $Images)) { Write-Host "[FAIL] 找不到图片目录: $Images" -ForegroundColor Red; exit 1 }

# 标签目录没给的话, 猜一个: "...\val\images" -> "...\val\labels"
if (-not $Labels) {
    $guess = Join-Path (Split-Path $Images -Parent) "labels"
    if (Test-Path $guess) { $Labels = $guess }
}
if (-not $Labels -or -not (Test-Path $Labels)) {
    Write-Host "[FAIL] 找不到标签目录, 请用 -Labels 指定" -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------------------
#  1. 解析 [result] 行
#     格式:  [result] <名字> balls=<n> [cx=.. cy=.. r=.. prop=..]*
#     多目标时取第一个球 —— vb_nms 是按置信度降序排的, 所以第 0 个就是最可信的。
#     balls 的数量本身也有用: 它直接反映"一帧里多报了没有"。
# ---------------------------------------------------------------------------
$det = @{}
foreach ($line in (Get-Content $Result -Encoding UTF8)) {
    if ($line -notmatch '^\[result\]\s+(\S+)\s+balls=(\d+)') { continue }
    $name  = $Matches[1]
    $nball = [int]$Matches[2]

    $cx = 0.0; $cy = 0.0; $r = 0.0; $prop = 0.0
    if ($line -match 'cx=([-\d.]+)\s+cy=([-\d.]+)\s+r=([-\d.]+)\s+prop=([-\d.]+)') {
        $cx = [double]$Matches[1]; $cy = [double]$Matches[2]
        $r  = [double]$Matches[3]; $prop = [double]$Matches[4]
    }
    $det[$name] = [PSCustomObject]@{ n = $nball; cx = $cx; cy = $cy; r = $r; prop = $prop }
}
Write-Host ("[1] 解析到 [result] 行 : " + $det.Count)

# ---------------------------------------------------------------------------
#  2. 逐张对比
# ---------------------------------------------------------------------------
$rows = New-Object System.Collections.ArrayList
foreach ($f in (Get-ChildItem $Images -File)) {
    $lp = Join-Path $Labels ($f.BaseName + ".txt")
    if (-not (Test-Path $lp)) { continue }
    $labels = @(Get-Content $lp | Where-Object { $_.Trim() -ne "" })

    $im = [System.Drawing.Image]::FromFile($f.FullName)
    $W  = $im.Width; $H = $im.Height; $im.Dispose()

    $d = $det[$f.Name]
    if ($null -eq $d) { $d = [PSCustomObject]@{ n = -1; cx = 0.0; cy = 0.0; r = 0.0; prop = 0.0 } }
    $hit = ($d.n -gt 0)

    if ($labels.Count -eq 1) {
        $p   = $labels[0].Trim() -split '\s+'
        $tcx = [double]$p[1] * $W
        $tcy = [double]$p[2] * $H
        $tr  = (([double]$p[3] * $W) + ([double]$p[4] * $H)) / 4.0

        if ($hit) {
            $dx = $d.cx - $tcx; $dy = $d.cy - $tcy; $dr = $d.r - $tr
            $d2 = [math]::Sqrt($dx * $dx + $dy * $dy)
        } else { $dx = $null; $dy = $null; $dr = $null; $d2 = $null }

        [void]$rows.Add([PSCustomObject]@{
            name = $f.Name; W = $W; H = $H; nlabel = 1
            tcx = $tcx; tcy = $tcy; tr = $tr
            ok = $hit; nball = $d.n; prop = $d.prop
            dx = $dx; dy = $dy; dr = $dr; d2 = $d2
        })
    } else {
        [void]$rows.Add([PSCustomObject]@{
            name = $f.Name; W = $W; H = $H; nlabel = $labels.Count
            tcx = 0; tcy = 0; tr = 0
            ok = $hit; nball = $d.n; prop = $d.prop
            dx = $null; dy = $null; dr = $null; d2 = $null
        })
    }
}

$single = @($rows | Where-Object { $_.nlabel -eq 1 })
$multi  = @($rows | Where-Object { $_.nlabel -ne 1 })
$hit    = @($single | Where-Object { $_.ok })
$miss   = @($single | Where-Object { -not $_.ok })
# 单目标图里"报了不止一个球" = 多报。这是最直接的误检指标。
$over   = @($hit | Where-Object { $_.nball -gt 1 })

Write-Host ""
Write-Host "=============== val 集精度统计 ==============="
Write-Host ("val 图片总数          : " + $rows.Count)
Write-Host ("  单目标              : " + $single.Count)
Write-Host ("  多目标(不参与统计)  : " + $multi.Count)
Write-Host ("单目标 检出           : " + $hit.Count)
Write-Host ("单目标 漏检           : " + $miss.Count)
if ($single.Count -gt 0) {
    Write-Host ("检出率                : " + [math]::Round(100.0 * $hit.Count / $single.Count, 1) + " %") -ForegroundColor Green
}
if ($hit.Count -gt 0) {
    $color = if ($over.Count -gt 0) { "Yellow" } else { "Green" }
    Write-Host ("多报(一帧报了>1个)    : " + $over.Count + " / " + $hit.Count +
                " = " + [math]::Round(100.0 * $over.Count / $hit.Count, 2) + " %") -ForegroundColor $color
}
$noresult = @($rows | Where-Object { $_.nball -eq -1 })
if ($noresult.Count -gt 0) {
    Write-Host ("程序根本没输出的图    : " + $noresult.Count + "  (检查 bi_log.txt)") -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
function ShowStat($arr, $label) {
    $a = @($arr | Where-Object { $null -ne $_ })
    if ($a.Count -eq 0) { Write-Host ("{0,-14} : (none)" -f $label); return }
    $s    = @($a | Sort-Object)
    $mean = ($a | Measure-Object -Average).Average
    $med  = $s[[int]($s.Count / 2)]
    $p90  = $s[[math]::Min($s.Count - 1, [int]($s.Count * 0.9))]
    $mx   = $s[-1]
    Write-Host ("{0,-14} : n={1,4}  mean={2,7:N2}  median={3,7:N2}  p90={4,7:N2}  max={5,8:N2}" -f `
        $label, $a.Count, $mean, $med, $p90, $mx)
}

Write-Host ""
Write-Host "--- 误差分布（原图像素）---"
ShowStat (@($hit | ForEach-Object { [math]::Abs($_.dx) }))   "abs dx"
ShowStat (@($hit | ForEach-Object { [math]::Abs($_.dy) }))   "abs dy"
ShowStat (@($hit | ForEach-Object { $_.d2 }))                "center dist"
ShowStat (@($hit | ForEach-Object { [math]::Abs($_.dr) }))   "abs dr"

Write-Host ""
Write-Host "--- 相对误差（除以真值半径, %）---"
ShowStat (@($hit | ForEach-Object { 100.0 * $_.d2 / $_.tr }))               "dist / r  (%)"
ShowStat (@($hit | ForEach-Object { 100.0 * [math]::Abs($_.dr) / $_.tr }))  "|dr| / r  (%)"

Write-Host ""
Write-Host "--- 按球半径分组的检出率（原图像素）---"
$bins = @(0, 20, 40, 80, 160, 320, 100000)
for ($i = 0; $i -lt $bins.Count - 1; $i++) {
    $lo = $bins[$i]; $hi = $bins[$i + 1]
    $g  = @($single | Where-Object { $_.tr -ge $lo -and $_.tr -lt $hi })
    $gh = @($g | Where-Object { $_.ok })
    if ($g.Count -gt 0) {
        Write-Host ("  r {0,5} - {1,-7} : {2,3}/{3,3} = {4,5:N1} %" -f `
            $lo, $hi, $gh.Count, $g.Count, (100.0 * $gh.Count / $g.Count))
    }
}

Write-Host ""
Write-Host "--- 漏检清单（单目标, 按半径排序）---"
if ($miss.Count -eq 0) { Write-Host "  (无)" }
else { $miss | Sort-Object tr | ForEach-Object { Write-Host ("  {0,-16} 真值 r = {1,7:N1} px" -f $_.name, $_.tr) } }

Write-Host ""
Write-Host "--- 置信度最低的 8 个检出 ---"
$hit | Sort-Object prop | Select-Object -First 8 | ForEach-Object {
    Write-Host ("  {0,-16} prop={1,5:N3}  r={2,7:N1}  dist={3,6:N1}" -f $_.name, $_.prop, $_.tr, $_.d2)
}

Write-Host ""
Write-Host "--- 多报的图（单目标却报了 >1 个球）---"
if ($over.Count -eq 0) { Write-Host "  (无)" }
else {
    $over | Sort-Object { $_.d2 } -Descending | Select-Object -First 10 | ForEach-Object {
        Write-Host ("  {0,-16} balls={1}  dist={2,8:N1}  r={3,7:N1}" -f $_.name, $_.nball, $_.d2, $_.tr)
    }
    if ($over.Count -gt 10) { Write-Host ("  ... 还有 " + ($over.Count - 10) + " 张") }
}

Write-Host ""
Write-Host "--- 明显跑飞（检出位置离真值 > 2 倍半径）---"
$fp = @($hit | Where-Object { $_.d2 -gt 2.0 * $_.tr })
Write-Host ("  个数 = " + $fp.Count)
$fp | Sort-Object { $_.d2 } -Descending | ForEach-Object {
    Write-Host ("  {0,-16} dist={1,8:N1}  r={2,7:N1}  prop={3,5:N3}" -f $_.name, $_.d2, $_.tr, $_.prop)
}
