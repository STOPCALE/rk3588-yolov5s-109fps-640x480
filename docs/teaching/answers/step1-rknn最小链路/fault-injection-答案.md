# step1 · 故障注入答案（对照 `step1-rknn最小链路/fault-injection.md`）

---

## 故障 1 · 维度读反

**根因（一句话）**：推导输入尺寸时**把 NCHW 读法写死了**（`c=dims[1], h=dims[2], w=dims[3]`），
而 best.rknn 输入是 **NHWC**（`fmt=1`）—— `dims[1]` 被当成"通道"，读出 `channel=640`。

**参考排查路径**：
1. 打印显示 `fmt=1`（NHWC）却出现 `channel=640, width=3` —— 两者矛盾，说明**读法没按 fmt 分支**。
2. 对照实测属性：NHWC 应 `h=dims[1]=640, w=dims[2]=640, c=dims[3]=3`。
3. 证实：按正确读法手算 size=1,228,800，与探针实测的 `size=1228800` 吻合。

**修复（参考实现同款）**：

```c
if (in_attr.fmt == RKNN_TENSOR_NCHW)
{ channel = in_attr.dims[1]; height = in_attr.dims[2]; width = in_attr.dims[3]; }
else
{ height = in_attr.dims[1]; width = in_attr.dims[2]; channel = in_attr.dims[3]; }
```

**验证**：重跑 → 打印 `channel=3 width=640 height=640`，程序继续走通喂数→推理→取输出
（修复后能看到 `rknn_run avg ≈ 22.6 ms` 与三个输出头的 size）。

**"乘积一样为什么还全错"**：$1\times640\times640\times3$ 的乘积与维序无关（交换律），
但 h/w/c 的**语义**全错——后续创建 Mat、通道检查、letterbox 缩放目标都建立在错误语义上。

---

## 故障 2 · 内存只涨不落

**根因（一句话）**：`rknn_outputs_get` 拿到的输出缓冲**没有配套 `rknn_outputs_release`**——
驱动每帧分配的 ≈0.144 MB（best 输出总量）资源永不归还。

**参考排查路径**：
1. VmRSS 每 30 帧 +≈4.4 MB 线性增长 → 折算 147.6 KB/帧 ≈ 三头 size 合计 151,200 B。
2. 在代码里把"get 与 release 配对"数一遍：get 1 处，release **0 处**。
3. 结论：不是 NPU 泄漏，是 API 资源没还（对照 00 §5）。

**修复**：在消费完 outputs 后（**成功与失败路径都**）加：

```c
rknn_outputs_release(ctx, io_num.n_output, outputs);
```

**验证**：重跑 300 帧 → VmRSS 曲线**平移**（在基线上下小幅波动），不再单调上涨。
实测对比：未修复 38,448 → 78,312 KB（+39.9 MB）；修复后全曲线波动 <几 MB。

**深挖**：主工程 §1.5 修复的同款问题——`rknn_outputs_get` 失败路径也要 release，
见 `src/rkYolov5s.cc` 的 infer 段注释。
