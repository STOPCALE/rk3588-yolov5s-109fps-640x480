#!/bin/bash
# =============================================================================
#  tools/board.sh —— 板载快捷入口（直接在板子上用，不需要 PC、不需要 ssh）
# -----------------------------------------------------------------------------
#  【一次性设置】（推荐，之后就能用短命令 demo）：
#      echo "alias demo='bash ~/myproj/tools/board.sh'" >> ~/.bashrc
#      source ~/.bashrc
#
#  【用法】（板子上任意目录）
#      demo cam [帧数] [更多参数...]     # 相机实时跑（默认 900 帧；例: demo cam 900 --cam-fps 60）
#      demo vis [帧数]                   # 相机录一段"带标注视频"，自动合成 mp4（默认 600 帧）
#      demo video <视频路径> [帧数]      # 跑视频文件（默认 4000 帧）
#      demo pic <图片路径> [循环次数]    # 跑单张图片（默认 200 次）
#      demo test                         # 一键跑全部自测（现场编译 4 个工具）
#      demo lock                         # 锁频（要 sudo；测性能前跑，重启后失效）
#      demo status                       # 看：锁频 / 相机 / 当前版本
#      demo off                          # 安全关机（sync + 关机，等指示灯灭再断电）
#      demo help
# =============================================================================
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"   # 工程根目录（脚本在 <根>/tools/ 下）
RUNSH="$ROOT/tools/run-demo.sh"
MODEL="./model/RK3588/best.rknn"           # 相对安装目录

cmd="${1:-help}"
[ $# -gt 0 ] && shift

case "$cmd" in
  cam)
    frames="${1:-900}"; [ $# -gt 0 ] && shift
    exec bash "$RUNSH" "$MODEL" /dev/video0 "$frames" --quiet --pipe "$@"
    ;;
  vis)
    frames="${1:-600}"; [ $# -gt 0 ] && shift
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
  lock)
    sudo bash "$ROOT/tools/lock-freq.sh" lock
    ;;
  status)
    bash "$ROOT/tools/lock-freq.sh"
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
  cam   [帧数] [更多参数]      相机实时跑（默认 900 帧）
  vis   [帧数]                 相机录标注视频→自动合成 mp4（默认 600 帧）
  video <视频文件> [帧数]      跑视频文件（默认 4000 帧）
  pic   <图片> [循环次数]      跑单张图片（默认 200 次）
  test                        一键跑全部自测（现场编译 4 个工具）
  lock                        锁频（要 sudo；测性能前跑）
  status                      状态总览（锁频/相机/版本）
  off                         安全关机（sync + poweroff）
TXT
    ;;
esac
