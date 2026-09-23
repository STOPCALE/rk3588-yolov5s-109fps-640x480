# step3 · 提示分级卡（卡壳时翻，不卡不看）

> L3 使用规则不变：翻到 L3 ＝ 当天合上资料凭记忆重做 + 登记到附录 E。

---

## L1 · 方向（一句话指路）

本步只有两件事：**"按正确地址取到 15 个数"**（offset/预筛）与
**"把数翻译回坐标"**（解码公式）。任何异常先分类：
- **找不到**（候选=0）→ 地址算错（offset 的系数顺序/plane 号）；
- **找得到但坐标怪** → 解码公式错（σ、stride、anchor、平方）。

## L2 · 检查点

| 症状 | 先查什么 |
|---|---|
| 候选=0 | offset 系数：(6a+f) 还是写成了别的组合？obj 是 `+4*glen` 吗？ |
| 候选"该有几个却没有几个" | 阈值 int8 算对了吗？比较方向（`<` 跳过）对吗？ |
| cx/cy 呈比例错误 | stride 是反推的还是写死的？ |
| 框整体偏大/偏小 | anchor 表选对头了吗？平方写了吗？ |
| 半径差 2 倍 | `r=(bw+bh)/4` 还是写成了 `/2`？ |
| 一堆重复框 | NMS 没跑/阈值错/没按 prop 降序 |

## L3 · 关键片段（翻到此级 = 合上重做 + 登记）

```c
const int base = 6 * a * glen;                  // anchor a 的 plane 0 起点
const int idx  = base + i * gw + j;             // 第 0 个字段 (x) 的下标
const int8_t obj = data[idx + 4 * glen];        // obj 在第 4 个 plane！
if (obj < thres_i8) continue;                   // 整数预筛
float tx = deqnt(data[idx + 0*glen], zp, scale);   // σ 输出 ∈[0,1]
float cx = (tx*2.0f - 0.5f + j) * stride;
float bw = bw_raw * bw_raw * anchor_w;          // 记得平方
float r  = (bw + bh) * 0.25f;                   // /4
```

构建运行故障小项目：

```bash
cd ~/myproj/docs/teaching/fault-injection/step3-01-候选消失   # 或 PC 上同目录
g++ -std=c++14 -O2 cand_gone.cc -o cand && ./cand
```
