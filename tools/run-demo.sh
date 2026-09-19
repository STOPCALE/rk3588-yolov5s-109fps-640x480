#!/bin/bash
# =============================================================================
#  tools/run-demo.sh  ——  用「正确姿势」跑 demo
#
#  它做两件事：
#      1. 自动找到安装目录（不用再 cd）
#      2. 自动绑大核 taskset -c 4-7
#
#  【为什么一定要绑大核】
#      实测：同一个程序，跑在小核(A55)上 preprocess 慢 1.86 倍，
#            整体帧率明显下降。而且调度器**什么时候**把你挪走是不确定的
#            —— 我们观察到"跑了 0.8 秒之后突然变慢"的跳变。
#      实时系统最怕的就是这种"不确定"。
#
#  【用法】（在板子上）
#      bash ~/myproj/tools/run-demo.sh ./model/RK3588/best.rknn ~/testimg/vb640.jpg 200
#
#      只绑核、不跑（想用别的方式跑）：
#      taskset -c 4-7 ~/myproj/install/my_rknn_yolov5_demo_aarch64/my_rknn_yolov5_demo ...
# =============================================================================

set -eu

# 脚本在 <工程根>/tools/ 下，所以根目录 = 上一级
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN_DIR="$ROOT/install/my_rknn_yolov5_demo_$(uname -m)"
BIN="$BIN_DIR/my_rknn_yolov5_demo"

if [ ! -x "$BIN" ]; then
    echo "!! 找不到可执行文件: $BIN"
    echo "   先在 PC 上跑一次 .\\tools\\sync.ps1 把它同步过去"
    exit 1
fi

# 提醒：如果没定频，数据不可信
gov=$(cat /sys/class/devfreq/dmc/governor 2>/dev/null)
if [ "$gov" != "performance" ]; then
    echo "⚠️  dmc governor = $gov （不是 performance）—— 测出来的性能数据不可信！"
    echo "    建议先跑:  sudo bash $ROOT/tools/lock-freq.sh lock"
    echo
fi

# ./model/... 这类相对路径是相对安装目录的
cd "$BIN_DIR"

# exec: 用 taskset 替换当前进程，不多起一层 shell
exec taskset -c 4-7 "$BIN" "$@"
