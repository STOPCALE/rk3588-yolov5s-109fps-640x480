# 📚 教学体系（teaching/）· 学习路径总表

> **这是什么**：把本工程改造成"**先考后看**"的重建教材——每步先做题、先预测、先手算，
> 再看参考实现；**题目与答案物理分离**（答案一律在 `answers/` 下，绝不混排）。
>
> **数字口径**：所有期望值、耗时、命中率都来自 `docs/00-AI协作上下文.md` §6 与
> 《手算与公式手册》的实测/手算数据；教学材料不编造数字。
>
> **配套**：故障注入小项目全部放 `fault-injection/` 下（独立目录、可单独编译、**不含修复线索**）。

---

## 0. 每个 step 的固定组成（7 + 3 + N）

| 文件 | 作用 |
|---|---|
| `concept-map.md` | 概念地图：逐块讲解 + 概念清单（搜索关键词/资料）+ 逐行预检表（✅/❓） |
| `predict-first.md` | 开代码前先写下答案的问题（只给"对照出处"，不给答案） |
| `hand-calc.md` | 手算题（对齐《手算与公式手册》21 式：每式至少"1 复现 + 1 变式"） |
| `self-test.md` | 复述题 / 追问链（连追 5 层）/ 变式题 |
| `fault-injection.md` | 故障注入说明书（症状 + 排查工作表三栏） |
| `hints.md` | 三级提示卡：L1 方向 → L2 检查点 → L3 关键片段 |
| `acceptance.md` | 验收清单（可勾选、可测量、含"白纸默写"项） |
| `answers/<step>/` | **答案三件套**：hand-calc / self-test / fault-injection 答案 |
| `fault-injection/<step>-NN-症状名>/` | 独立可编译的"故意坏版本"（README = 中性说明） |

> ⭐ **L3 使用规则（硬性）**：翻到提示卡的 L3 ＝ 当天合上资料、凭记忆重做该部分，
> 并到《AI代写代码清单（附录 E）》的 **L3 登记区**补一行。

## 1. 学习路径总表（按顺序走）

| step | 目录 | 核心内容 | 你做什么 | 验收标志（摘要） |
|---|---|---|---|---|
| **0** | `step0-cpp-骨架` | CMake 四阶段 / 构建类型兜底 / RPATH / install 规则 / 同步工作流 | 白纸写 CMakeLists 骨架 → 跑通 check/sync → 空壳上板 | 4 命令背出；`-O3` 与 `$ORIGIN/lib` 校验；2 个故障修出 |
| **1** | `step1-rknn最小链路` | RKNN init 七步 + infer 四件套 + 属性查询（NCHW/NHWC、zp/scale） | 白纸重写链路 → 修故障项目作"最小推理程序"上板 | dims/输出头打印正确；run ≈22.4ms；2 个故障归零 |
| **2** | `step2-letterbox坐标还原` | letterbox 正向四步 + 还原两步（先减 pad 再除 scale） | 手算 vb1/vb2 参数 → 还原公式实现 → 可视化故障修复 | vb1 打印 `scale=0.5 pads(140,140,0,0)`；红圈=绿圈 |
| **3** | `step3-后处理` | int8 预筛 + offset 布局 + 解码（σ/stride/anchor）+ NMS + 半径 | 手算 offset/解码/阈值 → 写预筛+解码+NMS → 修合成故障 | 候选 15→1；cx/cy ±0.5px；故障输出 `(311.0,250.0)` |
| **4** | `step4-视频循环计时` | read=解码（经验式）/ 定频三件套 / 帧预算 / 视频节流定律 / GStreamer 后端 | 用对照实验拆 read → 会锁频绑核 → 复现节流与解除 | 全速 ≈41.7fps；`read+infer=33.2`；相机故障 ≥100fps |
| **5** | `step5-多线程流水线` | 三根柱子（深拷贝/背压/锁外等待）+ 三线程流水线 + Little 定律 | 复认并发 bug → 假模型池实验 → 修两个确定性故障 | selftest 3/3；全速 ≥100fps；两故障数值达标 |
| **6** | `step6-串口收口` | termios 裸模式 / 15B 协议 + CRC / 真实时间戳预测 / 门限与锚点 | 手算弹道与带宽 → 解析实抓包 → 修 PTY 故障 | LOOPBACK PASS；151/151 发包；`0x0D→0x0A` 归零 |

**推荐节奏**：每步 1~2 天——先 `predict-first`（10 分钟）→ 带着问题读 `concept-map` →
写代码/做实验 → `hand-calc` 对账 → `self-test` → `fault-injection` → 用 `acceptance` 打勾。
卡住时按 `hints` 三级取提示（翻 L3 记得登记）。

## 2. 故障注入清单（独立小项目）

| 目录 | 一句话症状 | 运行环境 | 来源 |
|---|---|---|---|
| `step0-01-运行变慢` | 同样代码慢几倍（找不到 -O3） | 任意有 cmake | §1.5 / 案例：BUILD_TYPE 缺失 |
| `step0-02-文件打不开` | `./model/data.txt` 找不到（目录丢层） | 任意有 cmake | install(DIRECTORY) 尾斜杠 |
| `step1-01-维度读反` | 打印出 `channel=640, width=3` | 板子（RKNN） | 属性读法（NHWC/NCHW） |
| `step1-02-内存只涨不落` | VmRSS 每帧 +147KB | 板子（RKNN） | §1.5：outputs_get 无 release |
| `step2-01-红圈不套球` | 框恒偏移（忘减 pad） | 板子（OpenCV） | B7-3 漏/错还原的家族 |
| `step3-01-候选消失` | 合成张量里找不到植入的球 | PC/板（纯 C++） | offset 布局读错 |
| `step4-01-相机掉一半` | 120fps 只能跑 62fps | 板子 + 摄像头 | V4L2 BUFFERSIZE=1（案例：方法论 21） |
| `step5-01-帧被偷换` | tag mismatch 20/20 | PC/板（纯 C++） | cv::Mat 浅拷贝竞态（B8-2 P0-1） |
| `step5-02-流水线串行化` | 41.5 vs 124.1 fps 台阶 | PC/板（纯 C++） | 阻塞 get()（B8-3 真实 bug） |
| `step6-01-回车变异` | 1 字节 `0x0D→0x0A` | 板子（Linux PTY） | termios ICRNL（B9-1） |

> 另：`docs/teaching/tools/` 下有**张量属性探针** `probe_attrs.cc`（打印任意 .rknn 的输入输出属性）。

## 3. 与既有文档的关系

- 数字与结论出处：`00-AI协作上下文.md` §6、`手算与公式手册.md`（21 式编号在教材中直接引用）；
- 实验方法论：`调试与实验方法论.md`（教材的故障注入多来自其中真实案例）；
- 工具与验证：`验证与测量手册.md`、`tools/`（bench/probe/selftest 系列）；
- 本教材**只增不改**：现有全部章节文档保持原样，teaching/ 是与之并行的"考场"。
