#!/bin/bash
# =============================================================================
#  tools/run-demo.sh —— 板端启动脚本（工具脚本，不是学习内容）
#
#  作用：固定好「程序目录 + 自动绑核 + 未锁频提醒」，把参数原样转发给程序。
#        不带任何参数时 = best.rknn + 摄像头 /dev/video0
#
#  在板子上使用：
#      bash ~/newproj/tools/run-demo.sh                                    # 摄像头
#      bash ~/newproj/tools/run-demo.sh ./model/RK3588/best.rknn ~/testimg/vb640.jpg
#      bash ~/newproj/tools/run-demo.sh ./model/RK3588/best.rknn /dev/video0 --quiet
#      （model / input / 之后的参数 = 原样转发给程序，写法同程序本身）
#
#  选项（放最前面）：
#      -b   先「构建 + 安装」再跑（改了代码但没走 sync.ps1 时用）
#      -n   不绑核（默认自动检测「最高频的核」并绑上去；RK3588 上 = cpu4-7）
#      -h   显示帮助
#
#  手动指定绑核（做对照实验时）：
#      CORES=6,7 bash tools/run-demo.sh
#
#  从 PC 上一条命令执行：
#      ssh board "bash ~/newproj/tools/run-demo.sh"
# =============================================================================
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"      # 脚本放在 tools/ 下 → 上一级是工程根
APP_DIR="$ROOT/install/study_rknn_yolo_demo_aarch64"
CORES="${CORES:-}"    # 绑核列表；留空 = 自动检测（最高频的核），可用环境变量手动覆盖

# ---------------- 自动检测大核（最高频的核；RK3588 上 = cpu4-7） ----------------
detect_big_cores() {
    local best=0 v f c list=""
    # 第一遍：找出最高频率（小核 1.8G、大核 2.256G → 最大者即大核频率）
    for f in /sys/devices/system/cpu/cpu*/cpufreq/cpuinfo_max_freq; do
        [ -r "$f" ] || continue
        v="$(cat "$f")"
        if [ "$v" -gt "$best" ]; then best="$v"; fi
    done
    if [ "$best" -eq 0 ]; then
        return 1                  # 读不到任何频率 → 检测失败
    fi
    # 第二遍：收集所有等于最高频率的核号
    for f in /sys/devices/system/cpu/cpu*/cpufreq/cpuinfo_max_freq; do
        [ -r "$f" ] || continue
        v="$(cat "$f")"
        if [ "$v" -eq "$best" ]; then
            c="${f%/cpufreq/cpuinfo_max_freq}"   # → /sys/devices/system/cpu/cpu7
            c="${c##*/cpu}"                      # → 7
            list="${list:+$list,}$c"             # → 4,5,6,7
        fi
    done
    echo "$list"
}

# ---------------- 选项解析 ----------------
BUILD=0
BIND=1
while [ $# -gt 0 ]; do
    case "$1" in
        -b)  BUILD=1; shift ;;
        -n)  BIND=0;  shift ;;
        -h)
            echo "用法: bash $0 [选项] [model] [input] [程序参数...]"
            echo "  -b  先构建+安装再跑        -n  不绑核（默认自动绑最高频的核）"
            echo "  默认: ./model/RK3588/best.rknn  /dev/video0（摄像头）"
            exit 0 ;;
        *)   break ;;
    esac
done

# ---------------- 可选：构建 + 安装 ----------------
if [ "$BUILD" = 1 ]; then
    echo "==> 构建（cmake + make + install）"
    cmake -S "$ROOT" -B "$ROOT/build"
    cmake --build "$ROOT/build" -j"$(nproc)"
    cmake --install "$ROOT/build"
    echo "==> 构建完成"
fi

# ---------------- 检查已安装的程序 ----------------
if [ ! -x "$APP_DIR/study_rknn_yolo_demo" ]; then
    echo "[FAIL] 找不到 $APP_DIR/study_rknn_yolo_demo"
    echo "       先构建: bash $0 -b   （或在 PC 上跑 .\\tools\\sync.ps1）"
    exit 1
fi

# ---------------- 未锁频提醒（性能数字的守门员） ----------------
gov="$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_governor 2>/dev/null || true)"
if [ "$gov" != "performance" ]; then
    echo "[提示] CPU 未锁频（governor=$gov）；测性能前先执行："
    echo "       sudo bash ~/myproj/tools/lock-freq.sh lock"
fi

# ---------------- 自动绑核（默认开） ----------------
if [ "$BIND" = 1 ]; then
    if [ -z "$CORES" ]; then
        CORES="$(detect_big_cores || true)"
    fi
    if [ -n "$CORES" ]; then
        echo "==> 自动绑核: cpu $CORES（最高频集群）"
    else
        echo "[提示] 未检测到大核，本次不绑核运行"
        BIND=0
    fi
fi

# ---------------- 默认参数：best.rknn + 摄像头 ----------------
if [ $# -eq 0 ]; then
    set -- "./model/RK3588/best.rknn" "/dev/video0"
fi

cd "$APP_DIR"
echo "==> 运行: ./study_rknn_yolo_demo $*"
if [ "$BIND" = 1 ]; then
    exec taskset -c "$CORES" ./study_rknn_yolo_demo "$@"
else
    exec ./study_rknn_yolo_demo "$@"
fi
