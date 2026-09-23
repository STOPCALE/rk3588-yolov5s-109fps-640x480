# step2 · 提示分级卡（卡壳时翻，不卡不看）

> L3 使用规则不变：翻到 L3 ＝ 当天合上资料凭记忆重做 + 登记到附录 E。

---

## L1 · 方向（一句话指路）

本步只有**两个公式**：正向 `scale=min(640/W, 640/H)` + 居中补边；
反向 `x_orig=(x−pads.left)/scale`。**任何偏移，先问"偏移的形态"**：
- 偏移是**常数**（如恒定 +80）→ 与 pad 有关的某一项被漏了/加错方向；
- 偏移是**比例**（如全部 ×2.666）→ 与 scale 有关的某一项被漏了；
- 两者都有 → 大概率整体顺序写反。

## L2 · 检查点

| 症状形态 | 先查什么 |
|---|---|
| 所有框恒定下移 +N | 还原时减了/漏了 `pads.top` 吗？符号对吗？ |
| 所有框恒定左移/右移 | `pads.left` 同上；`left = dw/2` 的整数分半对吗？ |
| 坐标成比例放大（1/scale） | 漏除 `scale`（或乘反了） |
| 框位置"大概对但差一点" | 正向阶段 `(int)` 截断（±1px 正常，>2px 有鬼） |
| 快路径（640×480）下反而正常 | 检查还原公式在 scale=1、pads=0 时是否退化为恒等（应当） |

## L3 · 关键片段（翻到此级 = 合上重做 + 登记）

```c
// 正向（letterbox 里）
float scale = std::min((float)target.width / src.cols, (float)target.height / src.rows);
int new_w = (int)(src.cols * scale), new_h = (int)(src.rows * scale);
int dw = target.width - new_w, dh = target.height - new_h;
int left = dw / 2, top = dh / 2;
// ⚠️ copyMakeBorder 的顺序：(top, bottom, left, right)
cv::copyMakeBorder(scaled, dst, top, dh - top, left, dw - left, cv::BORDER_CONSTANT, color);

// 反向（还原）
x_orig = (x_model - pads.left) / scale;
y_orig = (y_model - pads.top)  / scale;
```

板上构建可视化故障项目：

```bash
cd ~/myproj/docs/teaching/fault-injection/step2-01-红圈不套球
cmake -B build -S . && cmake --build build -j4
./build/restore ~/testimg/vb640.jpg out.png
```
