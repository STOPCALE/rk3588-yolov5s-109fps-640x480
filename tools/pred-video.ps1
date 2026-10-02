# =============================================================================
#  tools\pred-video.ps1 —— 一键：某个视频 → 板端轨迹预测+标注 → 评估结果回传 PC
# -----------------------------------------------------------------------------
#  用法（在工程根目录；参数 1 = 视频路径）：
#     .\tools\pred-video.ps1 "E:\desk\logs\video\VID_20261002_163316.mp4"
#     .\tools\pred-video.ps1 "D:\clip.mp4" -OutDir "E:\desk\logs\video\我的输出"
#     .\tools\pred-video.ps1 clip.mp4 -Frames 8000      # 长视频提高处理上限
#     .\tools\pred-video.ps1 clip.mp4 -KeepBoard        # 保留板端临时文件（排查用）
#
#  它做的四件事（全用现有板端工具，不新增板端组件）：
#     ① scp 上传 → 板子 ~/testimg/（临时名 pv_<时间戳>.mp4，规避中文/空格）
#     ② 板端跑：run-demo.sh --quiet --fast --pipe --vis /tmp/pvv --pred-log /tmp/pvv.csv
#     ③ 合成 mp4：帧率 = 落盘帧数 x 源帧率 / 源帧数
#        （把显示线程偶尔丢的 0~几帧按均匀处理，播放速度≈原速）
#     ④ predictor_eval 回放评估（完整逐帧表+统计存入 eval.txt）
#     ⑤ scp 回传三件套 → 默认存「视频同目录\<视频名>_trace\」
#
#  成功自动清理板端临时文件；失败保留现场（板端 /tmp/pv* 与上传视频）。
#  本文件须为 "UTF-8 with BOM"（PS 5.1 才能正确读中文）。
# =============================================================================
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $Video,

    [string] $OutDir   = "",
    [int]    $Frames   = 4000,
    [switch] $KeepBoard
)

$ErrorActionPreference = "Stop"

# ---------------------------- 配置区（按需修改） ----------------------------
$BOARD     = "board"                       # ~/.ssh/config 里的别名
$BOARD_DIR = "~/myproj"                    # 板端工程目录
$MODEL     = "./model/RK3588/best.rknn"    # 相对安装目录（run-demo.sh 会自动 cd）
# ----------------------------------------------------------------------------

function Step($m) { Write-Host "==> $m" -ForegroundColor Cyan }
function Done($m) { Write-Host "[OK] $m" -ForegroundColor Green }
function Fail($m) { Write-Host "[FAIL] $m" -ForegroundColor Red; exit 1 }

# ---------- 0. 本地检查 + 板子连通 ----------
if (-not (Test-Path $Video)) { Fail "找不到视频: $Video" }
$Video = (Resolve-Path $Video).Path
$base  = [IO.Path]::GetFileNameWithoutExtension($Video)
if (-not $OutDir) { $OutDir = Join-Path (Split-Path $Video -Parent) ($base + "_trace") }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Force -Path $OutDir | Out-Null }

$hello = ssh $BOARD "echo BOARD_OK"
if ("$hello" -notmatch "BOARD_OK") { Fail "ssh $BOARD 不通——先确认板子在线（ssh board）" }

$rname  = "pv_" + (Get-Date -Format "MMddHHmmss") + [IO.Path]::GetExtension($Video)
$rvideo = "~/testimg/" + $rname

Step "视频  : $Video"
Step "输出  : $OutDir"
Step "板端名: $rname"

# ---------- 1. 上传 ----------
Step "[1/5] 上传到板子（$rvideo）..."
scp $Video ("${BOARD}:" + $rvideo)
if ($LASTEXITCODE -ne 0) { Fail "scp 上传失败" }

# ---------- 2. 探源信息（合成帧率折算用） ----------
$probe = ssh $BOARD ("ffprobe -v error -select_streams v:0 -show_entries stream=nb_frames,r_frame_rate -of default=noprint_wrappers=1 " + $rvideo)
$nf = 0; $num = 60; $den = 1
foreach ($line in @($probe)) {
    if ($line -match '^nb_frames=(\d+)')           { $nf  = [int]$Matches[1] }
    if ($line -match '^r_frame_rate=(\d+)/(\d+)')  { $num = [int]$Matches[1]; $den = [int]$Matches[2] }
}
if ($nf -gt $Frames) {
    Write-Host ("[warn] 源视频 {0} 帧 > 处理上限 {1} —— 只处理前 {1} 帧（全程请加 -Frames 更大值）" -f $nf, $Frames) -ForegroundColor Yellow
}

# ---------- 3. 板端：预测 + 标注帧落盘 ----------
Step "[2/5] 板端跑流水线（预测+标注，全速）..."
$runCmd = 'rm -rf /tmp/pvv /tmp/pvv.csv /tmp/pv_out.mp4 && mkdir -p /tmp/pvv && bash ' + $BOARD_DIR + '/tools/run-demo.sh ' + $MODEL + ' ' + $rvideo + ' ' + $Frames + ' --quiet --fast --pipe --vis /tmp/pvv --pred-log /tmp/pvv.csv > /tmp/pv_run.log 2>&1; echo RUN=$?; tail -n 10 /tmp/pv_run.log'
ssh $BOARD $runCmd

# ---------- 4. 清点落盘帧数 + 合成 mp4 ----------
$wraw = ssh $BOARD 'ls /tmp/pvv 2>/dev/null | wc -l'
$w = 0; [void][int]::TryParse((@($wraw) -join '').Trim(), [ref]$w)
if ($w -le 0) { Fail "没有落盘标注帧——到板子看 /tmp/pv_run.log 排查（临时文件已保留）" }

if ($nf -gt 0) { $fx = ("" + ($w * $num) + "/" + ($nf * $den)) }
else           { $fx = ("" + $num + "/" + $den); Write-Host "[warn] 未读到源帧数，合成按源帧率（有丢帧时速度会略快）" -ForegroundColor Yellow }

Step ("[3/5] 合成 mp4（落盘 {0} 帧 / 源 {1} 帧；播放帧率 = {2}）..." -f $w, $nf, $fx)
ssh $BOARD ('ffmpeg -y -framerate ' + $fx + ' -i /tmp/pvv/f_%06d.jpg -c:v libx264 -pix_fmt yuv420p -crf 23 /tmp/pv_out.mp4 -loglevel error; echo FF=$?')

# ---------- 5. 预测评估 ----------
Step "[4/5] predictor_eval 回放评估（默认 lead=25ms）..."
$evalCmd = 'cd ' + $BOARD_DIR + ' && g++ -Wall -Wextra -std=c++14 -Iinclude src/predictor_eval.cc src/trajectory_predictor.cc -o /tmp/predictor_eval && /tmp/predictor_eval /tmp/pvv.csv > /tmp/pv_eval.txt; tail -n 4 /tmp/pv_eval.txt'
ssh $BOARD $evalCmd

# ---------- 6. 回传 ----------
Step "[5/5] 回传到 PC ..."
$localMp4  = Join-Path $OutDir ($base + "_trace.mp4")
$localCsv  = Join-Path $OutDir ($base + "_pred.csv")
$localEval = Join-Path $OutDir ($base + "_eval.txt")
scp ("${BOARD}:/tmp/pv_out.mp4")  $localMp4
if ($LASTEXITCODE -ne 0) { Fail "scp 回传 mp4 失败" }
scp ("${BOARD}:/tmp/pvv.csv")     $localCsv
scp ("${BOARD}:/tmp/pv_eval.txt") $localEval

# ---------- 7. 清理板端 ----------
if (-not $KeepBoard) {
    ssh $BOARD ('rm -rf /tmp/pvv /tmp/pvv.csv /tmp/pv_out.mp4 /tmp/pv_eval.txt /tmp/pv_run.log ' + $rvideo) | Out-Null
}

Write-Host ""
Done "完成。结果文件（双击 mp4 即可检查）："
Write-Host "  $localMp4"
Write-Host "  $localCsv"
Write-Host "  $localEval"
