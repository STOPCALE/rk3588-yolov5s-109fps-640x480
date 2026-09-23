#!/usr/bin/env python3
# =============================================================================
#  e1_mpp_vs_pil_check.py —— B10/E1：mppjpegdec 硬解结果 vs PIL 软解 逐像素核对
# -----------------------------------------------------------------------------
#  用法（板子上）：
#    python3 tools/b10/e1_mpp_vs_pil_check.py <raw_bgr 文件(640x480x3)> <参考 jpg>
#  原理：
#    raw BGR 是 mppjpegdec 的输出（gst 管道 dump 的裸帧，BGR888）；
#    参考 jpg 用 PIL（独立软件解码实现）解出 RGB 后手动换序成 BGR；
#    两者逐字节对比 —— 期望差为 0~3（不同解码器的舍入差异），
#    若差异巨大说明硬解输出格式/颜色空间不对劲。
# =============================================================================
import sys
from PIL import Image

if len(sys.argv) < 3:
    print("用法: python3 e1_mpp_vs_pil_check.py <raw_bgr> <参考jpg>")
    sys.exit(1)

raw_path, jpg_path = sys.argv[1], sys.argv[2]
raw = open(raw_path, "rb").read()

im = Image.open(jpg_path).convert("RGB")
data = im.tobytes()
w, h = im.size
print("参考图 %dx%d，raw 字节数=%d，期望=%d" % (w, h, len(raw), w * h * 3))

if len(raw) != w * h * 3:
    print("⚠️ raw 尺寸不符（dump 可能失败）")
    sys.exit(1)

# RGB -> BGR（与硬解输出的通道顺序对齐）
bgr = bytearray(data)
bgr[0::3] = data[2::3]
bgr[1::3] = data[1::3]
bgr[2::3] = data[0::3]

mx = 0
neq = 0
for i in range(len(raw)):
    d = raw[i] - bgr[i]
    if d < 0:
        d = -d
    if d > mx:
        mx = d
    if d:
        neq += 1

print("MPP硬解 vs PIL软解: 最大像素差=%d，不一致字节=%d/%d（%.2f%%）" %
      (mx, neq, len(raw), neq * 100.0 / len(raw)))
