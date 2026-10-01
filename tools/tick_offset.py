# tools/tick_offset.py —— 测量 (墙钟 - 单调钟) 偏移，实验辅助脚本（诊断性质）
# 用途：把程序里 cv::getTickCount()（CLOCK_MONOTONIC）时间戳换算成板子墙钟：
#         wall_ms = mono_ms + offset*1000
# 说明：同一次开机内该偏移稳定（无 NTP 阶跃时），实验前后各测一次即可。
import time

for _ in range(3):
    w = time.time()
    m = time.monotonic()
    print("offset = %.6f s   (wall %.6f  mono %.6f)" % (w - m, w, m))
