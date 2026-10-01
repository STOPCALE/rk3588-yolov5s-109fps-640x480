# tools/led_analyze.py —— LED 亮度序列分析（诊断脚本，为光参照延迟实验服务）
# 用法（板子上）：
#   python3 led_analyze.py /tmp/led_on/b_mid.jpg --roi 235,236,263,264 [--ch G]    # 单图
#   python3 led_analyze.py /tmp/lat2/vis --roi 248,164,278,194 --csv out.csv --ch G
#       # 目录：输出每帧 ROI 亮度（用于自动找 LED 亮/灭跳变帧）
# 说明：--ch 选通道 R/G/B/L（默认 L）。绿色 LED 必须用 G 通道（红色灯/背景不干扰）。
#       ROI 为 (x0,y0,x1,y1)，以 640x480 画面为准。
import sys, os, glob
from PIL import Image

def main():
    src = sys.argv[1]
    roi = (238, 238, 262, 262)
    out = None
    ch = "L"
    args = sys.argv[2:]
    i = 0
    while i < len(args):
        if args[i] == "--roi":
            roi = tuple(map(int, args[i + 1].split(","))); i += 2
        elif args[i] == "--csv":
            out = args[i + 1]; i += 2
        elif args[i] == "--ch":
            ch = args[i + 1].upper(); i += 2
        else:
            i += 1

    files = sorted(glob.glob(os.path.join(src, "*.jpg"))) if os.path.isdir(src) else [src]
    fo = open(out, "w") if out else None
    if fo:
        fo.write("file,roi_mean,roi_max\n")

    for f in files:
        im = Image.open(f).convert("RGB")
        if ch == "L":
            crop = im.convert("L").crop(roi)
        else:
            idx = {"R": 0, "G": 1, "B": 2}[ch]
            crop = im.split()[idx].crop(roi)
        data = list(crop.getdata())
        m = sum(data) / len(data)
        mx = max(data)
        if out:
            if fo:
                fo.write("%s,%.1f,%d\n" % (os.path.basename(f), m, mx))
        else:
            print("%s  mean=%.1f  max=%d" % (os.path.basename(f), m, mx))

    if fo:
        fo.close()
        print("csv:", out)

if __name__ == "__main__":
    main()
