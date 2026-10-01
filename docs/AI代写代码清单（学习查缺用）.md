# 附录 E · AI 代写代码清单（学习查缺用）

> **为什么有这份文件**：B9（串口 + 预测器）和 B11（摄像头）的代码是**用户授权 AI 代写**的，
> 约定"摄像头检验通过后回头重学"。这份清单就是那时的**学习地图**：
> 每个文件 → 解决什么问题 → 涉及哪些知识点 → 怎么验证 → 学到什么程度算过关。
>
> **维护约定**：AI 每次代写新代码，必须在这里登记（AI 的固定职责之一）。
> 当前登记范围：**B9 全部 + B11 全部 + 8 个工具 + 教学改造（teaching/，见 §6）**。

---

## 0. 建议的学习方法（每份文件都这么过一遍）

1. **先跑一遍** → 看正常输出长什么样（验证命令在下面表格里）
2. **读文件头部注释** → 每个文件开头都写了"为什么这么设计"（最值钱的部分）
3. **对照文档** → 按"相关文档"列读背后的原理与实验数据
4. **自己重写** → 合上代码，照理解重新实现，和原版对比差异
5. **改参数做实验** → 在配置区改一个参数 → 先预测结果 → 跑一遍 → 对答案

---

## 1. B9 串口链路（Ch06 本体，写于 2026-09-20）

| 文件 | 规模 | 职责（一句话） | 知识点清单 | 验证命令（板子上） | 相关文档 |
|---|---|---|---|---|---|
| `src/uart.c` + `include/uart.h` | ~155 + 49 行 | 串口底层：termios **裸模式**配置 + open/read/write/close 4 个 C API | termios 各标志位含义；raw 模式要关哪些（ICANON / ECHO / ISIG / **INLCR / ICRNL / IGNCR** / IXON）；**VMIN/VTIME** 语义；`O_NDELAY` 的坑；部分写 + EINTR 重试；`tcflush`；"配置粘性"（程序退出后设置仍留在设备上） | `serial_selftest`（回环法） | `00` §6 P |
| `include/serial_proto.hpp` | ~101 行 | 15B 二进制包定义 + CRC-8 | 结构体对齐 `static_assert`；大端序 / 补码；CRC-8（poly=0x31, init=0xFF）位运算实现；协议骨架（同步头 A5 / CRC / 尾 5A） | `proto_selftest` | `00` §6 P；公式手册 §7.7 |
| `include/trajectory_predictor.hpp` + `src/trajectory_predictor.cc` | ~58 + 150 行 | 预测器：2~3 点 + 真实时间戳 → 速度 → **25ms 延迟补偿**外推；目标锁定状态机 | 案例 19（"像素/帧"错在哪）；`min_dt_ms` 防噪声；门限 gating（B8-4）；**锚点重锁**；状态机设计；配置区集中管理 | `predictor_selftest` / `predictor_eval` | `00` §6 Q、R 追记；方法论案例 19、20；公式手册 §8.6 |
| `src/main.cc` 的 **B9 增量**（文件本体是用户的，这些片段是 AI 写的） | 增量 | ① 参数解析 `--serial/--pred-log/--vis/--nogate`；② 结果循环：`predictor.update()` → 组包 → `uart_write()` → 统计；③ `--vis` 全套绘制（灰候选/绿锁定/红十字/橙尾迹/START/青预测十字/LAND 外推/状态文本）；④ 显示线程落盘 JPEG | 三线程数据流与"最新帧槽"覆盖语义；组包字段换算（图像中心相对偏差）；OpenCV 绘制 API；显示解耦（写盘放显示线程） | 全链路实跑（见 §4 命令） | `00` §6 Q、R；开发流程 §0.2.3 |

### B9 自测的预期输出（都是"全部 PASS"）

```bash
# 板子上（cwd = ~/myproj）
gcc -Wall -Wextra src/serial_selftest.c src/uart.c -Iinclude -o /tmp/serial_selftest && /tmp/serial_selftest /dev/ttyS4
g++ -Wall -Wextra -std=c++14 -Iinclude src/proto_selftest.cc -o /tmp/proto_selftest && /tmp/proto_selftest
g++ -Wall -Wextra -std=c++14 -Iinclude src/predictor_selftest.cc src/trajectory_predictor.cc -o /tmp/predictor_selftest && /tmp/predictor_selftest
```

### ⭐ 重点花时间的三处（最容易"照着抄但没懂"的地方）

1. **`uart.c` 的 INLCR / IGNCR / ICRNL** —— 不清掉它们，二进制包里的 `0x0D` 会被终端层**悄悄翻译成 `0x0A`**（数据被改，且不报错）
2. **预测器的速度单位** —— 原工程用"像素/帧"（暗含"帧间隔恒定"假设），丢帧/拒收时必错；正确做法是 px/**ms**（案例 19 有完整推导）
3. **`main.cc` 的前 5 包 hex 打印** —— 学协议时，用它和**手算**对照（`A5 00 20 FF ED 00 00 00 00 00 30 01 00 A9 5A` 那组的推导过程在 `00` §6 Q）

## 2. B11 摄像头（Ch05/Ch07 交界，写于 2026-09-20）

| 文件 | 职责 | 知识点清单 | 验证命令 | 相关文档 |
|---|---|---|---|---|
| `src/main.cc` 的 **B11 增量** | `/dev/video*` 检测 → `CAP_V4L2` 打开；缓冲=2；MJPG；`--cam-size/--cam-fps/--cam-yuyv` | V4L2/OpenCV 捕获路径；四字符码 FOURCC；**缓冲数与"每两帧丢一帧"**（案例 21）；"请求值 vs 实际生效值"的读法 | 相机运行命令（见 §4） | `00` §6 S；方法论案例 21 |
| `src/cam_probe.cc` | 相机体检：格式 / 实际 fps / read 耗时 / 逐秒漂移 / 样例图（不跑模型） | 解读 `v4l2-ctl --list-formats-ext`；"改了等于没改"的对照思路；逐秒帧率表看节流 | `./cam_probe /dev/video0 --fps 120 --sec 8` | `00` §6 S；开发流程 |

## 3. 工具类（AI 维护；思路必须懂，代码不必背）

| 工具 | 干什么 | 何时用 | 关键思路 |
|---|---|---|---|
| `src/bench_throughput.cc` | 3 实例 NPU 吞吐基准 | 改并发/绑核逻辑后回归 | 每 worker 自己循环（不经过主线程派发，避免混入调度开销） |
| `src/probe_video.cc` | 视频解码探针（"能解出多少帧"） | 视频打不开 / 卡死时 | 只读不解算，把解码问题隔离出来 |
| `src/pool_selftest.cc` | `rknnPool` 并发正确性（假模型，无需 NPU） | 改线程池后回归 | T1 深拷贝 / T2 满员拒收 / T3 锁外等待 |
| `src/serial_selftest.c` | 串口回环自测 | 动 `uart.c` 后 | 写 16 字节已知模式 → 读回逐字节比对；引脚短接法 |
| `src/proto_selftest.cc` | 协议层自测（PC 也能跑） | 动协议字段后 | 手算对照 + CRC 双实现 200 组随机对照 |
| `src/predictor_selftest.cc` | 预测器 T1~T6 | 动预测器后 | 合成数据 + 手算对照；T6 专测门限/锚点 |
| `src/predictor_eval.cc` | 预测质量评估（误差 vs 零阶保持基线） | 拿到实拍 CSV 后 | 基线对照思想；只在"两帧都有检测"处比对 |
| `src/cam_probe.cc` | 相机体检 | 换相机 / 换档位前 | 缓冲矩阵 + `v4l2-ctl` 独立实现交叉验证 |
| `src/cam_record.cc` | 摄像头录像（建数据集） | 拍训练素材 | 预跑估真实帧率；MJPEG 直通（`CONVERT_RGB=0`）零损失存原图；ffmpeg 放全核不抢采集大核；录制时屏幕预览（独立线程，不拖采集） |
| `tools/board.sh`（板载入口 `demo`） | 板子上敲 `demo cam/vis/test/lock/status/off` 的统一入口（已配 `~/bin/demo`） | 板子上日常使用（不在 PC 旁边时） | 包装 `run-demo.sh`（找安装目录+绑大核+锁频警告）；`readlink -f "$0"` 支持符号链接启动 |

### B10 验证工具（2026-09-23，`tools/b10/`）

> 背景：执行《B10优化验证报告》的全部实验（**用户指派：只验证不实现**）；报告在 `docs/B10优化验证报告.md`。

| 工具 | 干什么 | 关键思路 |
|---|---|---|
| `tools/b10/e1_decode_matrix.sh` | 解码链路对照（软/硬解、BGR 直出、DMABuf、相机链路、PIL 交叉验证） | 拼接 MJPEG 流摊薄启动开销；每项 timeout 兜底 |
| `tools/b10/e1_appsink_probe.cc` | OpenCV 交付路径探针（M0 基线/M1 直通/M2 vconv/M4 原始直读） | 同场多模式对照；`CONVERT_RGB=0` 分离"拷贝"与"解码"成本 |
| `tools/b10/e1_mpp_vs_pil_check.py` | 硬解裸帧 vs PIL 逐像素核对 | 独立解码实现交叉验证（BGR 对齐后逐字节比） |
| `tools/b10/e2_rga_probe.cc` | RGA 摆放/缩放耗时 + 正确性（含句柄版对照） | importbuffer 一次 + wrapbuffer_handle；CPU 对照作参照 |
| `tools/b10/e3_dmabuf_probe.cc` | DMA-BUF 零拷贝喂 NPU（fd 导入、set_io_mem、哈希对比） | FNV-1a 输出哈希做正确性判据；Z1b/Z1c/Z1d 逐步定位"首次绑定不生效" |

### 延迟验证工具（2026-10-01，`tools/`）

> 背景：完成"绝对端到端延迟"验证（用户指派：测试、验证、总结）；
> 报告在 `docs/端到端延迟验证报告.md`（`00` §6 U）。全部为**诊断工具/脚本**，不进入主链路。

| 工具 | 干什么 | 关键思路 |
|---|---|---|
| `tools/v4l2_time_probe.cc` | V4L2 内核时间戳：帧完成→用户态可见、真实帧周期 | 直接读 `buf.timestamp`（MONOTONIC）与 `clock_gettime` 差值；1/2/4 缓冲对照 |
| `tools/screen_clock.cc` + `shot.py` | 全屏毫秒时钟（板子墙钟）+ 远程截屏 | 屏幕法参照物（因 ~50ms 装置延迟弃用；数字仍作"投屏代价"） |
| `tools/tick_offset.py` | 测 wall−monotonic 偏移（µs 级、开机内恒定） | 对账的"钥匙"：把 cv::getTickCount 时间戳换算成墙钟 |
| `tools/led_seq_probe.cc` | 逐帧"ROI 亮度 + 帧时间戳"（主测量） | 亮度序列与时间轴一步到位，免人工读图 |
| `tools/led_flash.sh` | LED 闪烁 + µs 级开关时刻记录（EPOCHREALTIME，写前/写后区间） | 无 fork 取时；发光时刻不确定度 <1ms |
| `tools/led_diff.py` / `led_analyze.py` | 亮灭差分定位绿点 / 亮度序列提取 | G 通道专属指纹（红灯/白平衡不干扰）；自动推荐 ROI |
| `tools/led_eval.py` / `led_eval_full.py` | 跳变-帧对账（过渡帧法）/ 全跳变区间交叉验证 | 曝光 2.5ms 被切开的帧从亮度分数解码切割位置（单样本 ≤2.5ms） |
| `tools/crop_zoom.py` | 帧局部放大查看 | 目视核对定位 |

## 4. 掌握标准（自查表）

- [ ] 能画出 B9 数据流图：帧 → `predictor.update()` → 15B 包 → UART（并说出每步的时间量级）
- [ ] 能手算一个完整 15B 包（含 CRC），并用 `[pkt]` 打印对照
- [ ] 能解释：为什么速度用 px/ms 而不是 px/帧；`min_dt_ms` 防的是什么
- [ ] 能解释：门限 gating 和锚点重锁各自防的是哪种错（各举一个真实失败场景）
- [ ] 能解释：`BUFFERSIZE=1` 为什么把 120fps 变成 62fps
- [ ] 能不看旧代码独立写出 `uart.c` 的 raw 配置
- [ ] 能说出 `--vis` 每个标注元素的含义，并独立走一遍"跑 → 合成视频 → 取回 PC"
- [ ] 能说清 `demo` 的链路：`~/bin/demo`（符号链接）→ `tools/board.sh` → `tools/run-demo.sh` → 主程序

## 5. 相关文档导航

| 想知道 | 去哪 |
|---|---|
| B9 / B11 的实验数据与结论 | `00-AI协作上下文.md` §6 P、Q、R、**S** |
| 命令怎么敲（日常操作） | 附录 A《开发流程与常用指令》第 0 章 |
| 原理与公式（串口带宽、预测公式、弹道） | 附录 C《手算与公式手册》§7.7、§8.6 |
| 测试手段分类与测量陷阱 | 附录 B《验证与测量手册》 |
| 失败与教训（案例 19 / 20 / 21） | 附录 D《调试与实验方法论》 |
| B10 优化验证（硬解 / RGA / 零拷贝） | 《B10优化验证报告》（附录 F）；工具登记见本文 §3 末 |
| 绝对端到端延迟（LED 光参照，2026-10-01） | 《端到端延迟验证报告》；数据在 `E:\desk\logs\latency-probe\`；工具登记见本文 §3 末 |

## 6. 教学改造材料（teaching/，2026-09-23 全部由 AI 代写）

> 性质：**教材 + 考场基础设施**（不是工程本体）。入口：[teaching/README.md](teaching/README.md)。
> 用户的重写版本由用户自己写；这里的全部文件属于"给你出题的人和题目"。

| 目录 | 内容 | 规模 | 介绍 |
|---|---|---|---|
| `teaching/step0~step6-*` | 七个 step 的教材（每步 7 文件：概念图/先猜/手算/自测/故障注入/提示卡/验收） | 共 49 文件 | 对齐 21 式与 00 §6 数据；题答物理分离 |
| `teaching/answers/<step>/` | 答案三件套（hand-calc / self-test / fault-injection） | 21 文件 | **做完再看** |
| `teaching/fault-injection/` | 10 个"故意坏版本"独立小项目（step0-01 … step6-01） | 31 文件 | 清单与运行环境见 teaching/README.md §2；坏版本不含修复线索 |
| `teaching/tools/probe_attrs.cc` | 张量属性探针（打印任意 .rknn 的输入/输出属性、zp/scale、size_with_stride） | 1 文件 + cmake | 拿"属性类一手数据"用 |

**已验证状态（双态实测）**：step1/step2/step3/step4/step5/step6 的故障项目
均已在板子（或双平台）跑通"坏版本出现症状、修复版本指标达标"；
step0 的两个 CMake 故障为纯构建题（PC/板皆可复现）。

## 7. L3 提示使用登记区（翻到 L3 就登记）

> 规则：翻到任意 `hints.md` 的 **L3（关键片段）** ⇒ 当天合上资料凭记忆重做该部分，并在下表补一行。

| 日期 | step | 翻过的 L3 内容 | 已合上重做？ |
|---|---|---|---|
| | | | |
| | | | |
| | | | |
