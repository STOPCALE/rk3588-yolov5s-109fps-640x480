#!/bin/bash
# =============================================================================
#  tools/board.sh —— 板载快捷入口（直接在板子上用，不需要 PC、不需要 ssh）
# -----------------------------------------------------------------------------
#  【一次性设置】（推荐，之后就能用短命令 demo）：
#      mkdir -p ~/bin && ln -sf ~/myproj/tools/board.sh ~/bin/demo
#      （若 ~/bin 不在 PATH： echo "export PATH=/home/orangepi/bin:\$PATH" >> ~/.bashrc）
#      —— 已由 AI 在 2026-09-20 配好，新开终端直接敲 demo 即可
#
#  【用法】（板子上任意目录）
#      demo cam [帧数] [更多参数...]     # 相机实时跑（默认 900 帧；**0=无限模式**，Ctrl+C 停）
#      demo vis [帧数]                   # 相机录一段"带标注视频"，自动合成 mp4（默认 600 帧）
#      demo video <视频路径> [帧数]      # 跑视频文件（默认 4000 帧）
#      demo pic <图片路径> [循环次数]    # 跑单张图片（默认 200 次）
#      demo bg                           # **后台常驻**跑（相机无限模式；日志 /tmp/demo_run.log）
#      demo stop                         # 停止后台运行（兜底 pkill）
#      demo test                         # 一键跑全部自测（现场编译 4 个工具）
#      demo lock                         # 锁频（要 sudo；测性能前跑，重启后失效）
#      demo status                       # 看：锁频 / 后台 / 相机 / 当前版本
#      demo off                          # 安全关机（sync + 关机，等指示灯灭再断电）
#      demo help
# =============================================================================
set -u

SELF="$(readlink -f "$0")"                 # 解析符号链接（从 ~/bin/demo 运行也能找到真实路径）
ROOT="$(cd "$(dirname "$SELF")/.." && pwd)"   # 工程根目录（脚本在 <根>/tools/ 下）
RUNSH="$ROOT/tools/run-demo.sh"
MODEL="./model/RK3588/best.rknn"           # 相对安装目录

cmd="${1:-help}"
[ $# -gt 0 ] && shift

case "$cmd" in
  cam)
    frames="${1:-900}"; [ $# -gt 0 ] && shift
    if [ "$frames" = "0" ] || [ "$frames" = "inf" ] || [ "$frames" = "forever" ]; then
        echo "[board] 无限模式（Ctrl+C 停止）"
        exec bash "$RUNSH" "$MODEL" /dev/video0 1 --quiet --pipe --forever "$@"
    fi
    exec bash "$RUNSH" "$MODEL" /dev/video0 "$frames" --quiet --pipe "$@"
    ;;
  vis)
    frames="${1:-600}"; [ $# -gt 0 ] && shift
    if [ "$frames" = "0" ] || [ "$frames" = "inf" ] || [ "$frames" = "forever" ]; then
        echo "[board] ⚠️ vis 不支持无限（磁盘会被塞满）——请给帧数，例如 demo vis 1800"
        exit 1
    fi
    out="${VIS_OUT:-/tmp/rknn_vis}"
    rm -rf "$out"; mkdir -p "$out"
    echo "[board] 录制 $frames 帧 -> $out"
    bash "$RUNSH" "$MODEL" /dev/video0 "$frames" --quiet --pipe --vis "$out" "$@"
    echo "[board] 合成 mp4 ..."
    if ffmpeg -y -framerate 60 -i "$out/f_%06d.jpg" -c:v libx264 -pix_fmt yuv420p -crf 23 "$out.mp4" -loglevel error; then
        echo "[board] ✅ 标注视频: $out.mp4"
        echo "        （板子有桌面就双击打开；也可拷 U 盘带走）"
    else
        echo "[board] ⚠️ ffmpeg 合成失败（帧目录仍在: $out）"
    fi
    ;;
  video)
    file="${1:?用法: demo video <视频路径> [帧数]}"; [ $# -gt 0 ] && shift
    frames="${1:-4000}"; [ $# -gt 0 ] && shift
    exec bash "$RUNSH" "$MODEL" "$file" "$frames" --quiet --fast --pipe "$@"
    ;;
  pic)
    file="${1:?用法: demo pic <图片路径> [循环次数]}"; [ $# -gt 0 ] && shift
    n="${1:-200}"; [ $# -gt 0 ] && shift
    exec bash "$RUNSH" "$MODEL" "$file" "$n" "$@"
    ;;
  test)
    cd "$ROOT"
    echo "== 编译自测工具 =="
    gcc -Wall -Wextra src/serial_selftest.c src/uart.c -Iinclude -o /tmp/serial_selftest || exit 1
    g++ -Wall -Wextra -std=c++14 -Iinclude src/proto_selftest.cc -o /tmp/proto_selftest || exit 1
    g++ -Wall -Wextra -std=c++14 -Iinclude src/predictor_selftest.cc src/trajectory_predictor.cc -o /tmp/predictor_selftest || exit 1
    g++ -O2 -std=c++14 -Iinclude src/pool_selftest.cc -o /tmp/pool_selftest $(pkg-config --cflags --libs opencv4) -pthread || exit 1
    echo
    echo "== 1/4 串口回环（需要 pin16↔pin18 跳线）=="
    /tmp/serial_selftest /dev/ttyS4 || echo "⚠️ 未通过（跳线没插好？引脚位置见开发流程 §9.3）"
    echo
    echo "== 2/4 协议层 =="
    /tmp/proto_selftest
    echo
    echo "== 3/4 预测器 =="
    /tmp/predictor_selftest | tail -14
    echo
    echo "== 4/4 线程池（假模型，不占 NPU）=="
    taskset -c 4-7 /tmp/pool_selftest
    echo
    echo "[board] 自测结束"
    ;;
  bg)
    log="${DEMO_LOG:-/tmp/demo_run.log}"
    pidf=/tmp/demo.pid
    if [ -f "$pidf" ] && kill -0 "$(cat "$pidf")" 2>/dev/null; then
        echo "[board] 已在后台运行（PID $(cat "$pidf")）——先 demo stop 再启"
        exit 1
    fi
    nohup bash "$SELF" cam 0 "$@" > "$log" 2>&1 &
    echo $! > "$pidf"
    sleep 1
    if kill -0 "$(cat "$pidf")" 2>/dev/null; then
        echo "[board] ✅ 已后台启动（相机无限模式）PID=$(cat "$pidf")"
        echo "        日志: tail -f $log     停止: demo stop"
    else
        echo "[board] ⚠️ 启动失败，看日志: tail $log"
        rm -f "$pidf"
    fi
    ;;
  stop)
    pidf=/tmp/demo.pid
    if [ -f "$pidf" ]; then
        pid=$(cat "$pidf")
        if kill -0 "$pid" 2>/dev/null; then kill "$pid" && echo "[board] 已停止 PID=$pid"; fi
        rm -f "$pidf"
    fi
    pkill -f my_rknn_yolov5_demo 2>/dev/null
    exit 0
    ;;
  lock)
    sudo bash "$ROOT/tools/lock-freq.sh" lock
    ;;
  status)
    bash "$ROOT/tools/lock-freq.sh"
    echo
    echo "== 后台运行 =="
    if [ -f /tmp/demo.pid ] && kill -0 "$(cat /tmp/demo.pid)" 2>/dev/null; then
        ps -p "$(cat /tmp/demo.pid)" -o pid,etime,args --no-headers
    else
        echo "（未在后台运行）"
    fi
    echo
    echo "== 相机设备 =="
    ls -l /dev/video[0-9]* 2>/dev/null || echo "（未检测到 /dev/video*）"
    echo
    echo "== 当前版本 =="
    git -C "$ROOT" log --oneline -1 2>/dev/null || echo "(非 git 目录?)"
    ;;
  off)
    echo "[board] 正在同步磁盘并关机 —— 等指示灯灭后再断电"
    sync
    sudo poweroff
    ;;
  help|*)
    cat <<'TXT'
用法: demo <子命令> [参数...]
  cam   [帧数] [更多参数]      相机实时跑（默认 900；0=无限模式）
  vis   [帧数]                 相机录标注视频→自动合成 mp4（默认 600）
  video <视频文件> [帧数]      跑视频文件（默认 4000）
  pic   <图片> [循环次数]      跑单张图片（默认 200 次）
  bg                          后台常驻跑（相机无限模式；日志 /tmp/demo_run.log）
  stop                        停止后台运行（兜底 pkill）
  test                        一键跑全部自测（现场编译 4 个工具）
  lock                        锁频（要 sudo；测性能前跑）
  status                      状态总览（锁频/后台/相机/版本）
  off                         安全关机（sync + poweroff）
TXT
    ;;
esac
