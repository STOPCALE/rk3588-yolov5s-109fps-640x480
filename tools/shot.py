# tools/shot.py —— 远程截屏辅助脚本（诊断用，为屏幕时钟实验服务）
# 用法：DISPLAY=:0 python3 shot.py [输出png路径]
# 原理：优先 GTK Gdk 截图（稳），失败再试 xwd + PIL
import sys

out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/shot.png"

ok = False
try:
    import gi
    gi.require_version("Gdk", "3.0")
    from gi.repository import Gdk
    win = Gdk.get_default_root_window()
    w, h = win.get_width(), win.get_height()
    pb = Gdk.pixbuf_get_from_window(win, 0, 0, w, h)
    pb.savev(out, "png", [], [])
    print("saved(Gdk)", out, w, "x", h)
    ok = True
except Exception as e:
    print("Gdk failed:", e)

if not ok:
    import subprocess, io
    from PIL import Image
    raw = subprocess.run(["xwd", "-root", "-silent"], capture_output=True).stdout
    print("xwd raw bytes:", len(raw))
    img = Image.open(io.BytesIO(raw))
    img.save(out)
    print("saved(xwd+PIL)", out, img.size)
