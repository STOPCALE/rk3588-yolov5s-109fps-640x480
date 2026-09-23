# step2 · 故障注入答案（对照 `step2-letterbox坐标还原/fault-injection.md`）

---

## 故障 · 红圈不套球

**根因（一句话）**：还原函数**只除了 scale、漏减 pads**——
`x/scale` 而正确是 `(x−pads.left)/scale`。

**参考排查路径**：
1. 观察偏移形态：640×480 图上 delta_y **恒为 +80**；把 80 和打印的 pads.top=80 一比对 —— 数值巧合即线索。
2. 换公式验证：正确还原 = `(y−80)/1`；程序做的是 `y/1` → 差恰为 80。**恒定偏移 ⇔ 缺了平移项**。
3. 换 720×1280 图（pads.top=0、left=140）：delta 变成横向 +280 —— 偏移跟着"哪一个 pad 非零"走，
   彻底坐实。

**修复**：

```c
static cv::Point2f to_original(const LB &lb, float x, float y)
{
    return cv::Point2f((x - lb.left) / lb.scale,
                       (y - lb.top)  / lb.scale);
}
```

**验证**：重跑 → `delta = (0.0, 0.0)`，红圈与绿圈完全重合。

**如果这是主工程，症状长什么样**（B7-3 真实事故）：
编译通过、程序不报错、`--vis` 图上"有圈"，但（若 pads≠0）圈整体偏移；
更隐蔽的同类错误是**漏调还原**——形状全对，只有坐标数值错，**靠"检测值/真值恒等于 1/scale"
的比值分析**才能发现（本案 2.666）。
