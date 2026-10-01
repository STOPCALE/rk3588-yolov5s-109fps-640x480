# tools/led_eval.py —— 对账脚本：led.csv（闪烁记录）+ seq.csv（亮度序列）→ “发光→拿到帧”延迟
# 性质：诊断脚本（光参照延迟实验用）
#
# 原理：
#   - led.csv：每次开关记录 [before, after] 墙钟区间（μs 精度），取中点 = 发光/灭灯时刻
#   - seq.csv：每帧 t_ms（单调钟） + ROI 的 G 均值
#   - 被 LED 跳变“切开”的过渡帧（亮度为中间值）：Δ = wall(该帧) − led_switch 时刻
#     （误差 ≤ 曝光时长；本实验曝光 2.5ms）
#
# 用法（板子）：
#   python3 led_eval.py /tmp/lat5/led.csv /tmp/lat5/seq.csv --offset 1790837193.799795
import sys

def read_led(path):
    ev = []
    with open(path) as f:
        f.readline()
        for line in f:
            p = line.strip().split(",")
            if len(p) < 4:
                continue
            seq, state, b, a = float(p[0]), int(p[1]), float(p[2]), float(p[3])
            ev.append((seq, state, (b + a) / 2.0))
    return ev

def read_seq(path):
    rows = []
    with open(path) as f:
        f.readline()
        for line in f:
            p = line.strip().split(",")
            if len(p) < 4:
                continue
            rows.append((int(p[0]), float(p[1]), float(p[2]), float(p[3])))
    return rows

def main():
    led_csv, seq_csv = sys.argv[1], sys.argv[2]
    offset = None
    if "--offset" in sys.argv:
        offset = float(sys.argv[sys.argv.index("--offset") + 1])
    if offset is None:
        print("need --offset <wall-minus-mono seconds>")
        return

    led = sorted(read_led(led_csv), key=lambda x: x[2])
    seq = read_seq(seq_csv)

    gs = sorted(r[2] for r in seq)
    lo = gs[int(len(gs) * 0.10)]
    hi = gs[int(len(gs) * 0.90)]
    rng = hi - lo
    print("g stats: lo=%.1f hi=%.1f (frames=%d, led_switches=%d)" % (lo, hi, len(seq), len(led)))

    trans = [r for r in seq if lo + 0.15 * rng < r[2] < hi - 0.15 * rng]
    print("transition frames: %d" % len(trans))

    used = set()
    samples = []
    detail = []
    for (n, t_ms, g, gmax) in trans:
        t_wall = t_ms / 1000.0 + offset
        best = None
        for k, (sseq, sstate, st) in enumerate(led):
            d = t_wall - st
            if 0.0 <= d < 0.060 and k not in used:
                if best is None or d < best[0]:
                    best = (d, k, st, sstate)
        if best:
            used.add(best[1])
            samples.append(best[0] * 1000.0)
            detail.append((n, g, best[0] * 1000.0, best[3]))

    print("matched samples: %d" % len(samples))
    for (n, g, dms, st) in detail:
        print("  frame %d g=%.1f  state->%d  delta=%.2f ms" % (n, g, st, dms))
    if samples:
        ss = sorted(samples)
        mean = sum(ss) / len(ss)
        med = ss[len(ss) // 2]
        print("----------------")
        print("Δ(ms) sorted: " + ", ".join("%.1f" % s for s in ss))
        print("mean=%.2f  median=%.2f  min=%.2f  max=%.2f  (n=%d)" %
              (mean, med, ss[0], ss[-1], len(ss)))

if __name__ == "__main__":
    main()
