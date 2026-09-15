# =============================================================================
#  tools\sync.ps1
#  一键同步：PC 提交 → 推送到板子裸仓库 → 板子拉取 → 编译 → 安装
#
#  用法：
#     .\tools\sync.ps1                        # 用默认提交信息
#     .\tools\sync.ps1 -Message "加入后处理"   # 自定义提交信息
#     .\tools\sync.ps1 -NoBuild               # 只提交推送，不在板子上编译
#     .\tools\sync.ps1 -SkipCommit            # 不改代码，只想重新编译板子上的代码
#
#  首次使用前先设置 Git remote（只需一次）：
#     git remote add board board:~/myproj.git
#     git push -u board master
#
#  注：若运行时中文显示乱码，用 VS Code 把本文件另存为 "UTF-8 with BOM"
#      （PowerShell 5.1 需要 BOM 才能正确识别中文；乱码不影响功能）
# =============================================================================

param(
    [string] $Message    = "",
    [switch] $NoBuild,
    [switch] $SkipCommit
)

$ErrorActionPreference = "Stop"

# ---------------------------- 配置区（按需修改） ----------------------------
$PC_REPO   = "E:\desk\学习\C++\mytest\new"   # PC 上的工程目录（真源）
$BOARD     = "board"                          # ~/.ssh/config 中的别名
$BOARD_DIR = "~/myproj"                       # 板子上的工程目录
$REMOTE    = "board"                          # git remote 名（指向板子裸仓库）
# ----------------------------------------------------------------------------

function Step($m) { Write-Host "==> $m" -ForegroundColor Cyan  }
function Done($m) { Write-Host "[OK] $m" -ForegroundColor Green }
function Fail($m) { Write-Host "[FAIL] $m" -ForegroundColor Red; exit 1 }

if (-not (Test-Path $PC_REPO)) { Fail "PC_REPO 不存在: $PC_REPO" }
Set-Location $PC_REPO

$branch = (git rev-parse --abbrev-ref HEAD).Trim()
if (-not $branch) { Fail "无法获取当前分支名" }

# ---------------------------- 1. 提交 ----------------------------
if ($SkipCommit) {
    Step "跳过提交（-SkipCommit）"
}
else {
    $dirty = git status --porcelain
    if (-not $dirty) {
        Step "工作区无改动，跳过提交"
    }
    else {
        if (-not $Message) { $Message = "sync: " + (Get-Date -Format "yyyy-MM-dd HH:mm:ss") }
        Step "提交改动：$Message"
        git add -A
        git commit -m "$Message"
        if ($LASTEXITCODE -ne 0) { Fail "git commit 失败" }
        Done "已提交"
    }
}

# ---------------------------- 2. 推送到板子 ----------------------------
Step "推送到板子裸仓库（$REMOTE/$branch）..."
git push $REMOTE $branch
if ($LASTEXITCODE -ne 0) { Fail "git push 失败；请确认已执行 git remote add $REMOTE board:~/myproj.git" }
Done "已推送"

if ($NoBuild) { Done "完成（-NoBuild，未在板子上编译）"; exit 0 }

# ---------------------------- 3. 板子上拉取并编译 ----------------------------
Step "板子上拉取最新代码并编译..."
ssh $BOARD "cd $BOARD_DIR && git pull --ff-only && cmake -B build -S . && cmake --build build -j8"
if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "提示：用下面这条命令把完整日志取回本地查看" -ForegroundColor Yellow
    Write-Host "  ssh $BOARD `"cd $BOARD_DIR && cmake --build build -j8 2>&1 | tee /tmp/build.log`"" -ForegroundColor Yellow
    Write-Host "  scp ${BOARD}:/tmp/build.log ." -ForegroundColor Yellow
    Fail "板子上编译失败"
}
Done "编译成功"

# ---------------------------- 4. 安装 ----------------------------
Step "安装到部署目录..."
ssh $BOARD "cd $BOARD_DIR && cmake --install build"
if ($LASTEXITCODE -ne 0) { Fail "cmake --install 失败" }

Write-Host ""
Done "全部完成 🎉"
Write-Host ""
Write-Host "运行方式：" -ForegroundColor Cyan
Write-Host "  ssh $BOARD" -ForegroundColor Gray
Write-Host "  cd $BOARD_DIR/install/my_rknn_yolov5_demo_Linux" -ForegroundColor Gray
Write-Host "  ./my_rknn_yolov5_demo ./model/RK3588/best.rknn <视频路径>" -ForegroundColor Gray
