# tools/led_eval_full.py —— LED 光参照实验·全跳变交叉验证（诊断脚本，2026-10-01）
# 对每个 LED 跳变找"帧侧新态首帧"得 Δ_ub（= C + 采样相位），输出分布与区间约束；
# 与 led_eval.py（过渡帧高精度版）互为补充。
# 注：路径与时钟偏移（OFF）按实验现场修改——OFF 由 tick_offset.py 测得。
import io, sys

LED  = r"e:\desk\logs\latency-probe\lat5\led.csv"
SEQ  = r"e:\desk\logs\latency-probe\lat5\seq.csv"
OFF  = 1790837193.799795

def read_led(path):
    ev = []
    for line in open(path).read().splitlines()[1:]:
        p = line.split(",")
        if len(p) < 4: continue
        ev.append((int(float(p[0])), int(float(p[1])), (float(p[2]) + float(p[3])) / 2.0))
    return sorted(ev, key=lambda x: x[2])

def read_seq(path):
    rows = []
    for line in open(path).read().splitlines()[1:]:
        p = line.split(",")
        if len(p) < 4: continue
        rows.append((int(p[0]), float(p[1]), float(p[2]), float(p[3])))
    return rows

led = read_led(LED)
seq = read_seq(SEQ)

gs = sorted(r[2] for r in seq)
lo = gs[int(len(gs)*0.10)]
hi = gs[int(len(gs)*0.90)]
mid = (lo+hi)/2.0
print("g lo=%.1f hi=%.1f mid=%.1f frames=%d" % (lo, hi, mid, len(seq)))

state = [1 if r[2] > mid else 0 for r in seq]

changes = []
for i in range(1, len(seq)):
    if state[i] != state[i-1]:
        changes.append(i)
print("frame changes: %d   led events: %d" % (len(changes), len(led)))

used = set()
res = []
for k, (sseq, sstate, t_led) in enumerate(led):
    cands = []
    for i in changes:
        if i in used: continue
        t_wall = seq[i][1]/1000.0 + OFF
        d = (t_wall - t_led) * 1000.0
        if 0 <= d <= 40 and state[i] == sstate:
            cands.append((d, i))
    if cands:
        d, i = min(cands)
        gap = seq[i][1] - seq[i-1][1]
        res.append((k, sstate, d, gap, seq[i][2]))
        used.add(i)

print("paired: %d / %d" % (len(res), len(led)))
print(" k  dir  d_ub(ms)  gap(ms)  g_firstnew")
for r in res:
    print("%3d  ->%d  %7.2f  %7.2f  %6.1f" % (r[0], r[1], r[2], r[3], r[4]))

if res:
    lows  = [r[2] - max(2.5, r[3]) for r in res]
    ups   = [r[2] for r in res]
    lo_b  = max(lows); up_b = min(ups)
    print("interval intersection: [%.2f, %.2f] ms" % (lo_b, up_b))
    ds = sorted(r[2] for r in res)
    print("d_ub  min=%.2f  max=%.2f  mean=%.2f  (n=%d)" % (ds[0], ds[-1], sum(ds)/len(ds), len(ds)))
    # 高精度近似：d_ub - 期望的“帧内相位”，用中位数间隔的一半经验修正
    import statistics
    print("note: true delta ~= min(d_ub) .. min(d_ub)+frame_gap")
