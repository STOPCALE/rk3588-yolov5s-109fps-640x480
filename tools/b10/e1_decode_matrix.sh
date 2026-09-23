#!/bin/bash
# =============================================================================
#  e1_decode_matrix.sh —— B10/E1：图像解码链路对照验证（2026-09-23）
# -----------------------------------------------------------------------------
#  回答的问题：
#    ① mppjpegdec 硬解比软件解码（jpegdec / PIL）快多少？
#    ② mppjpegdec 直接输出 BGR / DMABuf 是否"免费"（硬件内部完成）？
#    ③ videoconvert（软件格式转换）代价多大？（对照用）
#    ④ 摄像头实时链路（v4l2src）能否把 640×480@120 档喂满？
#  前置：板子已定频（demo lock）；运行相机段时确保没有别的程序占相机
#  用法（板子上）：
#      bash ~/myproj/tools/b10/e1_decode_matrix.sh [帧目录] [圈数]
#  说明：文件链路用 loop=true 循环多圈，摊薄 gst-launch 启动开销（~0.1-0.2s）；
#        默认素材：~/videos/cam_20260923_125232 的 329 帧原始 JPEG（cam_record --jpg 录的）
# =============================================================================
set -u
D="${1:-$HOME/videos/cam_20260923_125232}"
LOOPS="${2:-10}"
PER=$(ls "$D"/frame_*.jpg 2>/dev/null | wc -l)
[ "$PER" -gt 0 ] || { echo "找不到帧序列: $D"; exit 1; }
N1=$((PER * LOOPS))
N2=$((PER * 2))
T="taskset -c 4-7"
run() { local label="$1"; shift; echo "=== $label ==="; time "$@"; echo "rc=$?"; }

echo "帧目录: $D（$PER 帧/圈，循环 $LOOPS 圈 = $N1 帧）"

# --- 文件序列：解码链路对照（全部走 benchmark，不含相机）---
run "1L-SW-jpegdec-x$LOOPS"    $T gst-launch-1.0 -q multifilesrc location=$D/frame_%06d.jpg caps=image/jpeg loop=true ! jpegparse ! jpegdec ! fakesink sync=false num-buffers=$N1
run "2L-HW-mppjpegdec-x$LOOPS" $T gst-launch-1.0 -q multifilesrc location=$D/frame_%06d.jpg caps=image/jpeg loop=true ! jpegparse ! mppjpegdec ! fakesink sync=false num-buffers=$N1
run "3L-HW-BGR-x$LOOPS"        $T gst-launch-1.0 -q multifilesrc location=$D/frame_%06d.jpg caps=image/jpeg loop=true ! jpegparse ! mppjpegdec ! video/x-raw,format=BGR ! fakesink sync=false num-buffers=$N1
run "4L-HW-DMABuf-x$LOOPS"     $T gst-launch-1.0 -q multifilesrc location=$D/frame_%06d.jpg caps=image/jpeg loop=true ! jpegparse ! mppjpegdec ! "video/x-raw(memory:DMABuf),format=BGR" ! fakesink sync=false num-buffers=$N1
run "5L-HW-vconv-BGR-x2"       $T gst-launch-1.0 -q multifilesrc location=$D/frame_%06d.jpg caps=image/jpeg loop=true ! jpegparse ! mppjpegdec ! videoconvert ! video/x-raw,format=BGR ! fakesink sync=false num-buffers=$N2

# --- 摄像头实时链路（600 帧/组）---
run "6-cam-raw"     $T gst-launch-1.0 -q v4l2src device=/dev/video0 num-buffers=600 ! image/jpeg,width=640,height=480,framerate=120/1 ! fakesink sync=false
run "7-cam-mpp"     $T gst-launch-1.0 -q v4l2src device=/dev/video0 num-buffers=600 ! image/jpeg,width=640,height=480,framerate=120/1 ! mppjpegdec ! fakesink sync=false
run "8-cam-mpp-BGR" $T gst-launch-1.0 -q v4l2src device=/dev/video0 num-buffers=600 ! image/jpeg,width=640,height=480,framerate=120/1 ! mppjpegdec ! video/x-raw,format=BGR ! fakesink sync=false

# --- 硬解一帧导出（目视正确性检查；拷回 PC 用看图软件确认）---
echo "=== DUMP-1frame（硬解 BGR 导出）==="
gst-launch-1.0 -q -e filesrc location=$D/frame_000000.jpg ! image/jpeg ! jpegparse ! mppjpegdec ! video/x-raw,format=BGR ! jpegenc ! filesink location=/tmp/b10_chk.jpg
ls -l /tmp/b10_chk.jpg

# --- PIL 独立软解交叉验证（排除"测量方法本身有问题"）---
echo "=== PIL 交叉验证（独立软件解码实现，1 圈）==="
python3 - "$D" <<'PYEOF'
import sys, time, glob
try:
    from PIL import Image
except Exception as e:
    print('PIL 不可用:', e); raise SystemExit
fs = sorted(glob.glob(sys.argv[1] + '/frame_*.jpg'))
t0 = time.time()
for f in fs: Image.open(f).load()
t1 = time.time()
print('PIL decode %d frames: %.3f s -> %.3f ms/frame' % (len(fs), t1-t0, (t1-t0)*1000/len(fs)))
PYEOF

echo ALLDONE
