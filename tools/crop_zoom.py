# tools/crop_zoom.py —— 裁剪放大（诊断脚本，目视检查 LED 细节）
# 用法：python3 crop_zoom.py <输入图> <输出图> <x0,y0,x1,y1> [放大倍数=4]
from PIL import Image
import sys

src, dst = sys.argv[1], sys.argv[2]
box = tuple(map(int, sys.argv[3].split(",")))
z = int(sys.argv[4]) if len(sys.argv) > 4 else 4

c = Image.open(src).convert("RGB").crop(box)
c = c.resize((c.size[0] * z, c.size[1] * z), Image.NEAREST)
c.save(dst)
print("saved", dst, c.size)
