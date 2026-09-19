# =============================================================================
#  tools\batch-infer.ps1  --  批量推理：本地一堆数据 -> 板子 -> 结果拉回来
#
#  用法:
#      .\tools\batch-infer.ps1 -Source "D:\data\val\images"
#      .\tools\batch-infer.ps1 -Source "C:\Users\HP\Videos\vb.mp4"
#      .\tools\batch-infer.ps1 -Source "D:\data\val\images" -SaveFrames
#      .\tools\batch-infer.ps1 -Source "C:\Users\HP\Videos\vb.mp4" -Verbose
#
#  参数:
#      -Source     本地文件夹(图片批量) 或 单个视频文件           [必填]
#      -Model      模型文件名(在 ./model/RK3588/ 下), 默认 best.rknn
#      -OutDir     结果拉回本地哪个目录, 默认 E:\desk\logs\批次时间戳
#      -Max        最多处理多少帧/张
#      -Verbose    传 --quiet 给程序: 顺便保留每帧的调试打印(默认是关掉的)
#      -SaveFrames 顺便把标注好的图/视频拉回来(方便人眼核对)
#      -NoUpload   跳过上传(数据已经在板子上了)
#
#  产出(放在 -OutDir 里):
#      bi_result.txt   只含 [result] 行  -> 喂给 tools\analyze-val.ps1 统计精度
#      bi_log.txt      程序完整输出      -> 出问题的时候看这个
#      bi_out\         标注图/标注视频   (只有加了 -SaveFrames 才有)
#
#  ---------------------------------------------------------------------------
#  为什么里面要绕一圈 base64?
#      ssh 传命令时, PowerShell -> ssh.exe -> 远程 bash 这条路上有三层引号规则,
#      单引号里的双引号会被吃掉, 文件名里的括号也会被当成语法。
#      于是干脆: 本地把 bash 脚本写好 -> base64 编码 -> 远程 base64 -d | bash。
#      base64 只有 [A-Za-z0-9+/=], 三层规则都不会碰它, 一次到位。
#      这个技巧以后写任何远程脚本都能直接抄。
#
#  NOTE: 本文件需要保存为 "UTF-8 with BOM" (PowerShell 5.1 才认中文).
# =============================================================================

param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $Source,

    [string] $Model      = "best.rknn",
    [string] $OutDir     = "",
    [int]    $Max        = 0,
    [switch] $Verbose,
    [switch] $SaveFrames,
    [switch] $NoUpload
)

$ErrorActionPreference = "Stop"

# ---------------------------- 配置区（按需修改） ----------------------------
$BOARD   = "board"                                   # ~/.ssh/config 里的别名
$REMOTE  = "testimg/in"                              # 板子上放数据的目录(相对家目录)
$DEPLOY  = '$HOME/myproj/install/my_rknn_yolov5_demo_aarch64'
# ----------------------------------------------------------------------------

function Step($m) { Write-Host "==> $m" -ForegroundColor Cyan }
function Done($m) { Write-Host "[OK] $m" -ForegroundColor Green }
function Fail($m) { Write-Host "[FAIL] $m" -ForegroundColor Red; exit 1 }

if (-not (Test-Path $Source)) { Fail "Source 不存在: $Source" }

$isDir = (Get-Item $Source).PSIsContainer

if (-not $OutDir) { $OutDir = "E:\desk\logs\" + (Get-Date -Format "yyyyMMdd-HHmmss") }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

# 版本号这些值会在 base64 之前拼进 bash, 所以先算好
$flags = if ($Verbose) { "" } else { "--quiet" }
if ($Max -gt 0) { $flags = ("$flags --max $Max").Trim() }

# ---------------------------------------------------------------------------
#  1. 上传
# ---------------------------------------------------------------------------
if ($NoUpload) {
    Step "跳过上传（-NoUpload）"
}
else {
    if ($isDir) {
        $n = @(Get-ChildItem -Path $Source -File).Count
        Step "打包 $n 个文件..." 
        $tgz = Join-Path $env:TEMP "rknn_batch.tgz"
        if (Test-Path $tgz) { Remove-Item $tgz -Force }
        # tar.exe 是 Win10 1803+ 自带的, -C 后跟目录, "." 表示目录内容
        & tar -czf $tgz -C $Source .
        if ($LASTEXITCODE -ne 0) { Fail "tar 打包失败" }

        Step "上传到板子（$([math]::Round((Get-Item $tgz).Length / 1MB, 1)) MB）..."
        # 先清空远程目录：上一次跑剩的文件要是留着，统计精度时会混进来，很难发现
        ssh $BOARD "mkdir -p $REMOTE && rm -rf $REMOTE/*"
        scp $tgz "${BOARD}:$REMOTE/payload.tgz"
        if ($LASTEXITCODE -ne 0) { Fail "scp 上传失败" }

        Step "板子上解压..."
        ssh $BOARD "cd $REMOTE && tar -xzf payload.tgz && rm -f payload.tgz"
        if ($LASTEXITCODE -ne 0) { Fail "板子上解压失败" }
    }
    else {
        $ext  = [System.IO.Path]::GetExtension($Source).ToLower()
        $dst  = if ($ext) { "input$ext" } else { "input" }
        $size = [math]::Round((Get-Item $Source).Length / 1MB, 1)
        Step "上传单个文件（$size MB）-> 板子 $REMOTE/$dst"
        ssh $BOARD "mkdir -p $REMOTE && rm -rf $REMOTE/*"
        scp $Source "${BOARD}:$REMOTE/$dst"
        if ($LASTEXITCODE -ne 0) { Fail "scp 上传失败" }
    }
    Done "上传完成"
}

# ---------------------------------------------------------------------------
#  2. 拼 bash 脚本 -> base64 -> 板上跑
# ---------------------------------------------------------------------------
$stage = if ($SaveFrames) { " --out /tmp/bi_out" } else { "" }

if ($isDir) {
    $runBlock = @'
for f in SRC/*; do
    ./my_rknn_yolov5_demo ./model/RK3588/MODEL "$f" FLAGSSTAGE >> /tmp/bi_log.txt 2>&1
done
'@
}
else {
    $runBlock = @'
./my_rknn_yolov5_demo ./model/RK3588/MODEL SRC/input.* FLAGSSTAGE > /tmp/bi_log.txt 2>&1
'@
}

$bash = @'
OUT=/tmp/bi_out
rm -rf $OUT; mkdir -p $OUT
: > /tmp/bi_log.txt
cd DEPLOY

RUNBLOCK

grep '^\[result\]' /tmp/bi_log.txt > /tmp/bi_result.txt || true
echo "RESULT_FILES=$(wc -l < /tmp/bi_result.txt)"
echo "OUT_FILES=$(ls -1 $OUT | wc -l)"
'@

# bash 变量 ${...} 和 $... 要原样送到远程, 所以全部用 .Replace() 注入, 不用 -f (大括号会打架)
$bash = $bash.Replace("DEPLOY",    $DEPLOY)
$bash = $bash.Replace("RUNBLOCK",  $runBlock.TrimEnd())
$bash = $bash.Replace("SRC",       ('$HOME/' + $REMOTE))
$bash = $bash.Replace("MODEL",     $Model)
$bash = $bash.Replace("FLAGSSTAGE", $stage)
$bash = $bash.Replace("FLAGS",     $flags)

$b64 = [Convert]::ToBase64String([System.Text.Encoding]::UTF8.GetBytes($bash))

Step "板子上推理中（可能要几分钟）..."
$remoteOut = ssh $BOARD "echo $b64 | base64 -d | bash"
if ($LASTEXITCODE -ne 0) { Fail "板子上执行失败" }
$remoteOut | ForEach-Object { Write-Host "    $_" -ForegroundColor Gray }

# ---------------------------------------------------------------------------
#  3. 拉回结果
# ---------------------------------------------------------------------------
Step "拉回结果到 $OutDir ..."
scp "${BOARD}:/tmp/bi_result.txt" (Join-Path $OutDir "bi_result.txt") | Out-Null
scp "${BOARD}:/tmp/bi_log.txt"    (Join-Path $OutDir "bi_log.txt")    | Out-Null

if ($SaveFrames) {
    scp -r "${BOARD}:/tmp/bi_out" $OutDir | Out-Null
}

Write-Host ""
Done "全部完成 🎉"
Write-Host "  结果   : $(Join-Path $OutDir 'bi_result.txt')" -ForegroundColor Gray
Write-Host "  日志   : $(Join-Path $OutDir 'bi_log.txt')"    -ForegroundColor Gray
if ($SaveFrames) {
    Write-Host "  标注   : $(Join-Path $OutDir 'bi_out')"      -ForegroundColor Gray
}

if ($isDir) {
    Write-Host ""
    Write-Host "下一步：统计精度（需要标签目录）"  -ForegroundColor Cyan
    Write-Host "  .\tools\analyze-val.ps1 -Result `"$(Join-Path $OutDir 'bi_result.txt')`" -Images `"$Source`"" -ForegroundColor Gray
}
