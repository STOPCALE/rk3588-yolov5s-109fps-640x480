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
#  说明（v1.1，2026-09-23）：
#    · 旧版用 multifilesrc loop=true 循环多圈 → 实测「mppjpegdec 段卡死」（记录：诊断案例），
#      改为预先把 N 圈 JPEG 首尾相接拼成一个 MJPEG 流（filesrc 一次读完），简单可靠
#    · 每项加 timeout 兜底（单项卡死不会拖垮整套实验）
#    · 默认素材：~/videos/cam_20260923_125232 的 329 帧原始 JPEG（cam_record --jpg 录的）
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

# --- 预拼一个大 MJPEG 流：N 圈 JPEG 首尾相接，filesrc 一次读完 ---
PREP=/tmp/b10_all10.mjpeg
if [ ! -f "$PREP" ] || [ "$(stat -c%s "$PREP" 2>/dev/null || echo 0)" -lt 1000000 ]; then
    echo "拼接 MJPEG 流（$LOOPS 圈）→ $PREP"
    ( cd "$D" && for i in $(seq 1 $LOOPS); do cat frame_*.jpg; done ) > "$PREP"
fi
ls -l "$PREP"

# --- 文件序列：解码链路对照 ---
run "1C-SW-jpegdec"    $T timeout 120 gst-launch-1.0 -q filesrc location=$PREP ! image/jpeg ! jpegparse ! jpegdec ! fakesink sync=false num-buffers=$N1
run "2C-HW-mppjpegdec" $T timeout 120 gst-launch-1.0 -q filesrc location=$PREP ! image/jpeg ! jpegparse ! mppjpegdec ! fakesink sync=false num-buffers=$N1
run "3C-HW-BGR"        $T timeout 120 gst-launch-1.0 -q filesrc location=$PREP ! image/jpeg ! jpegparse ! mppjpegdec ! video/x-raw,format=BGR ! fakesink sync=false num-buffers=$N1
run "4C-HW-DMABuf"     $T timeout 120 gst-launch-1.0 -q filesrc location=$PREP ! image/jpeg ! jpegparse ! mppjpegdec ! "video/x-raw(memory:DMABuf),format=BGR" ! fakesink sync=false num-buffers=$N1
run "5C-HW-vconv-BGR"  $T timeout 120 gst-launch-1.0 -q filesrc location=$PREP ! image/jpeg ! jpegparse ! mppjpegdec ! videoconvert ! video/x-raw,format=BGR ! fakesink sync=false num-buffers=$N2

# --- 摄像头实时链路（600 帧/组，timeout 兜底）---
run "6-cam-raw"     $T timeout 60 gst-launch-1.0 -q v4l2src device=/dev/video0 num-buffers=600 ! image/jpeg,width=640,height=480,framerate=120/1 ! fakesink sync=false
run "7-cam-mpp"     $T timeout 60 gst-launch-1.0 -q v4l2src device=/dev/video0 num-buffers=600 ! image/jpeg,width=640,height=480,framerate=120/1 ! mppjpegdec ! fakesink sync=false
run "8-cam-mpp-BGR" $T timeout 60 gst-launch-1.0 -q v4l2src device=/dev/video0 num-buffers=600 ! image/jpeg,width=640,height=480,framerate=120/1 ! mppjpegdec ! video/x-raw,format=BGR ! fakesink sync=false

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
