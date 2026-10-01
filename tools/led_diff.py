# tools/led_diff.py —— 亮/灭差分定位（诊断脚本，为光参照实验校准 ROI）
# 用法（板子上）：python3 led_diff.py <开灯图> <关灯图>
# 只统计 **G 通道** 变化（绿色 LED 的专属 fingerprints；红色灯/白平衡漂移不贡献 G 差）
# → 精确定位受控绿灯像素位置、推荐 ROI，并给出分离度。
import sys
from PIL import Image

a = Image.open(sys.argv[1]).convert("RGB")
b = Image.open(sys.argv[2]).convert("RGB")
w, h = a.size
da = list(a.getdata())
db = list(b.getdata())

pts = []
for i, (p, q) in enumerate(zip(da, db)):
    # “亮的绿点本体”：开灯图 G 高 且 关灯图 G 低（红灯/背景不满足此条件）
    if p[1] > 120 and q[1] < 60:
        pts.append((i % w, i // w, p[1] - q[1]))

print("green-LED pixels(ON G>120 & OFF G<60):", len(pts))
if pts:
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    cx = sum(xs) / len(xs)
    cy = sum(ys) / len(ys)
    print("bbox: x %d..%d  y %d..%d   centroid (%.1f, %.1f)" %
          (min(xs), max(xs), min(ys), max(ys), cx, cy))
    md = sum(p[2] for p in pts) / len(pts)
    print("mean dG %.1f" % md)

    x0 = max(0, int(cx) - 15)
    y0 = max(0, int(cy) - 15)
    box = (x0, y0, x0 + 30, y0 + 30)
    ga = list(a.split()[1].crop(box).getdata())
    gb = list(b.split()[1].crop(box).getdata())
    print("window %s : ON Gmean %.1f (max %d) vs OFF Gmean %.1f (max %d)" %
          (box, sum(ga) / len(ga), max(ga), sum(gb) / len(gb), max(gb)))
    print("=> recommended roi for analyze: %d,%d,%d,%d" % (x0, y0, x0 + 30, y0 + 30))
else:
    print("no significant GREEN change —— 绿灯没响应？检查接线/权限/灯泡")

