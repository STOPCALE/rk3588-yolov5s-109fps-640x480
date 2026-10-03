#!/bin/bash
# =============================================================================
#  tools/run-demo.sh —— 板端启动脚本（工具脚本，不是学习内容）
#
#  作用：固定好「程序目录 + 绑大核 + 自动锁频」，把参数原样转发给程序。
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
#      -n   不绑大核（默认 taskset -c 4-7，绑到 4 个大核）
#      -h   显示帮助
#
#  从 PC 上一条命令执行：
#      ssh board "bash ~/newproj/tools/run-demo.sh"
# =============================================================================
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"      # 脚本放在 tools/ 下 → 上一级是工程根
APP_DIR="$ROOT/install/study_rknn_yolo_demo_aarch64"
CORES="4-7"                                    # 绑核范围（RK3588: 4-7 为四个大核）

# ---------------- 选项解析 ----------------
BUILD=0
BIND=1
while [ $# -gt 0 ]; do
    case "$1" in
        -b)  BUILD=1; shift ;;
        -n)  BIND=0;  shift ;;
        -h)
            echo "用法: bash $0 [选项] [model] [input] [程序参数...]"
            echo "  -b  先构建+安装再跑        -n  不绑核（默认绑 4-7）"
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

# ---------------- 自动锁频（性能数字的守门员） ----------------
# 检查 CPU(policy0/4/6) + NPU + dmc 是否全部 performance
freq_all_locked() {
    for f in /sys/devices/system/cpu/cpufreq/policy0/scaling_governor \
             /sys/devices/system/cpu/cpufreq/policy4/scaling_governor \
             /sys/devices/system/cpu/cpufreq/policy6/scaling_governor \
             /sys/class/devfreq/fdab0000.npu/governor \
             /sys/class/devfreq/dmc/governor; do
        [ "$(cat "$f" 2>/dev/null)" = "performance" ] || return 1
    done
    return 0
}

if ! freq_all_locked; then
    echo "==> 未定频，尝试自动锁定（CPU/NPU/dmc）..."
    sudo -n /bin/bash "$ROOT/tools/lock-freq.sh" lock >/dev/null 2>&1 || true
    if freq_all_locked; then
        echo "==> 已锁定"
    else
        echo "[提示] 自动锁频失败（sudo 需要密码）。任选一种："
        echo "  1) 现在手动锁一次（会提示输密码）："
        echo "       ssh -t board \"sudo bash ~/newproj/tools/lock-freq.sh lock\""
        echo "  2) 想以后全自动，在板子上粘贴一次（输一次密码即可永久生效）："
        echo "       echo 'orangepi ALL=(ALL) NOPASSWD: /bin/bash /home/orangepi/newproj/tools/lock-freq.sh lock' | sudo tee /etc/sudoers.d/lock-freq"
    fi
else
    echo "==> 频率已锁定（跳过锁频）"
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
