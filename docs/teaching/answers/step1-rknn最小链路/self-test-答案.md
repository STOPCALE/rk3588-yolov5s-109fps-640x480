# step1 · 自测答案（对照 `step1-rknn最小链路/self-test.md`）

## 一、复述题要点

1. **init 七步**：① read_model——文件进内存；② rknn_init——建上下文（内部拷贝数据）；
   ③ set_core_mask——绑 NPU 核（并行必需）；④ SDK 版本——排查版本兼容；
   ⑤ IN_OUT_NUM——拿到循环边界；⑥ INPUT_ATTR——拿 dims/fmt 推导 h/w/c；
   ⑦ OUTPUT_ATTR——拿 dims/zp/scale（量化参数抄一份复用）。
2. **四件套**：inputs_set（喂）→ run（推）→ outputs_get（取）→ outputs_release（还）。
   漏"还"：**每帧泄漏＝输出总量**（best ≈0.144 MB/帧、yolov5s ≈2.14 MB/帧），且单调累积不回落。
3. `attr.index = i`：不写 → 查询读到的是**错误/未定义的张量**（常见为全零或上一张量），
   dims/scale 全不可信 → 尺寸错、结果错（见故障 1 案例的"维度读反"是同类问题的另一面）。
4. NCHW：`c=dims[1], h=dims[2], w=dims[3]`；NHWC：`h=dims[1], w=dims[2], c=dims[3]`。
   必须按 `fmt` 分支——本项目**输入是 NHWC(fmt=1)、输出是 NCHW(fmt=0)**，两个方向都可能踩坑，**不能假设**。

## 二、追问链（无标准答案，参考闭合点）

- 链 A 闭合点：init 后 ctx 内部已有模型 → model_data 可 free（主工程在析构才 free，是保守做法也行）；
  析构顺序 destroy(ctx) → free(model_data) → free(attrs)；先 free 数据再 destroy 一般也可，
  但**先 destroy 再 free 才是铁律方向**（ctx 活着时不得动它的内部数据）。
- 链 B 闭合点：Mat(解码后) → buf(指向 Mat 内存，零拷贝) → 驱动/输入缓冲（inputs_set，**一次拷贝**）→
  NPU 内部 → outputs 缓冲（**get 分配**，要还）→ 反量化到 float（后处理站）。
  量化在"NPU 侧"（输出是 int8，zp/scale 在 attr 里）；泄漏的是 outputs 站的资源。

## 三、变式题参考答案

**变式 1**：查三条——① 输入 buf 是不是每帧新建（缓存/对齐变化 → 带宽掉）；
② 是否有别的线程/进程同时抢内存带宽（DMC 被分走）；③ 缓存一致性开销（B10 结论：
DMA 写过的 buffer CPU 再读可能触发 `DMA_BUF_IOCTL_SYNC` 同步——见《B10优化验证报告》§3.3）。

**变式 2**：出问题的循环 = **按输出头遍历的循环**（后处理/属性数组都假定 3 头）。
防：`if (io_num.n_output != VB_HEAD_NUM) { 报错并 return; }`——主工程 §1.5 已加此校验。

**变式 3**：① 确认模型文件完整读入（打印 model_size，非零且字节数吻合）；
② 查版本兼容（SDK 1.6.0 与 driver 0.8.2 是否匹配、转换工具链版本是否一致）；
③ 换官方预转模型（yolov5s-640-640.rknn）交叉验证——区分"模型问题"还是"运行时问题"。
