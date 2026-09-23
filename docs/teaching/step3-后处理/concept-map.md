# step3 · 后处理（概念地图）

> 目标：掌握「151,200 个格子 → 15 个候选 → 1 个球（中心+半径）」的整条后处理链，
> 能用**手算**预告解码结果，并理解"先整数预筛、后浮点解码"的性能设计。
>
> 参考实现：`src/postprocess.cc`（`vb_decode()` / NMS / `vb_to_original()`）；
> 调用点：`src/rkYolov5s.cc` 中 `post_process(...)`（约 306 行）；
> 数据：00 §6 E/F；公式：手算手册 §1~§4。

---

## 一、整条链（每帧只花 0.043 ms 的秘密）

```
3 个输出头 (int8)                      151,200 格
   │  ① int8 整数预筛（obj > -64）      ← 一句话比较，-O3 自动向量化
   ▼
  15 个候选  →  ② 浮点解码（σ/stride/anchor）0.041 ms
   ▼
  15 →  ③ NMS 贪心抑制（IoU>0.45）      0.002 ms
   ▼
   1 个球：cx=311.1 cy=250.8 r=11.0      prop=0.941（实测，00 §6 F）
```

**②③只对 15 个幸存者做**——若把 151,200 格全部反量化，耗时会高**几百倍**（00 §6 E）。

## 二、五个核心公式（手算手册编号）

| 式 | 公式 | 用途 |
|---|---|---|
| **2** | `offset = (6a+f)·G + i·W + j`（[1,3,6,H,W]，**anchor 最外、字段居中**） | 取任意格子的任意字段 |
| **3** | `stride = model_w / grid_w`（**反推**，不写死） | 80/40/20 → 8/16/32 |
| **5** | `q = round(f/scale) + zp` | 阈值 0.25 → **-64**（只算一次） |
| **6** | `cx = (σ(tx)·2 − 0.5 + j)·stride` | 球心；`*2-0.5` 让中心能探出半格 |
| **7/8** | `bw = (σ(tw)·2)²·anchor_w`；`r = (bw+bh)/4` | 宽高平方再乘 anchor；半径 |

**锚点表**（`postprocess.cc` 顶部 `VB_ANCHORS`，YOLOv5 标准、B7-1 已实测匹配）：
头0 `{10,13, 16,30, 33,23}`／头1 `{30,61, 62,45, 59,119}`／头2 `{116,90, 156,198, 373,326}`。

## 三、概念清单（一句话 + 搜索关键词 + 资料）

| 概念 | 一句话 | 搜索关键词 | 推荐资料 |
|---|---|---|---|
| 量化/反量化 | int8 与实数间的线性映射 `real=(q−zp)·scale` | "rknn 量化 zero point scale" | 手册 §2；rknn-toolkit2 doc「量化原理」 |
| sigmoid 输出 | 本模型三个头输出已过 σ，real∈[0,1] | "yolov5 decode sigmoid" | yolov5 `detect.py`/`utils/general.py` |
| 网格/stride | 头越深格子越粗，负责越大的目标 | "yolov5 stride grid head" | 手册 §1.3；yolov5 官方结构图 |
| anchor | 典型目标尺寸先验（聚类得到） | "yolov5 anchors autoanchor" | 手册 §3.3；yolov5 `hyp.scratch.yaml` |
| NMS 贪心 | 按分数降序，抑制 IoU 超阈的重复框 | "NMS 贪心算法 IoU" | 手册 §4；CS231n 检测一节 |
| int8 预筛 | 阈值先量化成 int8，全程整数比较 | "int8 quantization threshold filter" | CSAPP 第 5 章（自动向量化/缓存） |
| L2 适配 | 151,200 B 全装进 L2 → 预筛飞快 | "cache blocking data fitting L2" | CSAPP §6；00 §6 E |
| IoU 的 +1.0 | 整数像素坐标系的老约定，改用浮点后要删 | "iou +1 pixel coordinate" | 00 §6 F 备注 |

## 四、逐行预检表（对 `postprocess.cc` 的 `vb_decode()`）

| 块 | 行为 | ✅/❓ |
|---|---|---|
| 1. `stride = model_w / gw` | 反推 stride（不写死 8/16/32） | |
| 2. `thres_i8 = qnt_f32(conf_thresh,…)` | 阈值只量化一次，循环里全整数 | |
| 3. 三层循环 `a → i → j` | **顺序**与内存布局 `[3,6,H,W]` 对应 | |
| 4. `obj = data[idx + 4*glen]` | obj 在第 4 个 plane；`idx` 含 anchor 基址 | |
| 5. `if (obj < thres_i8) continue;` | 一行筛掉 99% 格子 | |
| 6. 四条 `deqnt(...)` | 定 x/y/w/h 的原始值 | |
| 7. `cx = (bx_raw + j)*stride` | 先加格子索引，再乘 stride | |
| 8. `bw = bw_raw² * aw` | 平方 × anchor | |
| 9. `radius = (bw+bh)/4` | 半径 = (宽+高)/4 | |
| 10. NMS 排序 + 抑制 | 按 prop 降序贪心 | |

## 五、参考实现映射（写前只准看"位置"）

| 你的任务 | 参考位置 |
|---|---|
| 预筛 + 解码 | `src/postprocess.cc` `vb_decode()` |
| IoU / NMS | 同文件 `vb_iou()` + 抑制段 |
| 坐标还原 | 同文件 `vb_to_original()`（先减 pads 再乘 1/scale） |
| 调用与计时 | `src/rkYolov5s.cc` `infer()` 里 `post_process` 前后 |

## 六、硬约定（本步相关）

1. **offset 的系数顺序**：`(6a+f)` —— obj 系数必是 `4G`、cls 必是 `5G`（手册 §1.2 检查法）；
2. **stride 反推不写死**（换模型尺寸自动适配）；
3. 阈值 **预量化**，循环内只做整数比较；
4. NMS 阈值 0.45、置信度 0.25（与实测输出 `prop=0.941` 对照：远超阈值）。
