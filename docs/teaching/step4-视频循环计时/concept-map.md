# step4 · 视频循环与计时（概念地图）

> 目标：学会用「分段计时 + 对照实验」拆开一帧视频的每一毫秒——
> 看清 **read 的真身是解码**、**性能数字必须定频后才可信**、**视频文件默认被实时节流**。
>
> 参考：`src/main.cc` 的循环计时与 `--fast`/`--quiet`；工具 `src/probe_video.cc`、`bench_throughput`；
> 数据：00 §6 I/J/M 与手册 §7.1/7.5/7.6。

---

## 一、一帧视频的解剖（分段计时）

```
read(取帧) → preprocess(→640) → rknn_run(NPU) → post(后处理) → 画框/统计
```

`StageStat` 对每段做 avg/min/max —— **三个都要看**：avg 是稳态、min 是潜力、max 暴露抖动。

## 二、read 的真面目（一次著名的误判）

| 实验（BMP 对照，0 行代码） | 文件大小 | 耗时 |
|---|---|---|
| vb1.jpg（有损压缩） | 229 KB | **10.35~11.48 ms** |
| vb1.bmp（无压缩） | **2.64 MB（×11.8）** | **2.59 ms（÷4.4）** |

文件大 11.8 倍反而快 → **排除 IO、锁定 JPEG 解码**：
$$\texttt{cv::imread} = \underbrace{\text{读字节}}_{\approx 0.03\text{ms}} + \underbrace{\text{JPEG 解码}}_{\approx 10.3\text{ms}}\ (99.7\%)$$

**两参数经验式（手册式 16）**：$t[\text{ms}] \approx 7.5\times10^{-6}\cdot N_{px} + 16.8\times10^{-6}\cdot N_B$
（拟合误差：vb1 +1.2%、(67) −0.4%）

## 三、性能数字的前提：定频三件套 + 绑核

| 开关 | 现象 | 数字 |
|---|---|---|
| **dmc 528→2112 MHz** | run −19%、read −53%、抖动 ×7.8 稳定 | 27.6→**22.44 ms** |
| **taskset -c 4-7** | 消除 A76→A55 迁移（preprocess ×1.86 台阶） | 全程 0.09 ms |
| NPU performance | 1.0 GHz 固定 | — |

⚠️ **不锁 dmc = 19% 的误差**；不绑核 = 可能慢 1.86 倍。**测任何数字前先做这两件事。**

## 四、视频文件的"实时节流"之谜（B8-1d 重大发现）

**现象**：300 帧视频跑 10s+，且 verbose/quiet 三次测量的 `read+infer` **完全相同 = 33.23 ms**——
这不是巧合，是循环被**外部节流钉死**（按 30fps 播放）：
$$T_{\text{循环}} = \max(\text{节流周期},\ \text{单帧工作量})\quad(\text{手册式 17.7})$$

**破解**：自定义 pipeline `... ! appsink sync=false` 喂给 `cap.open(pipeline, CAP_GSTREAMER)`
→ `read` 从 ~10 ms 掉到 **0.11 ms**，循环 24 ms = **41.7 fps**（这才是真实力）。
主程序把它封装成 `--fast`。

## 五、GStreamer 后端事件簿（三个层次）

1. **FFMPEG 后端**（OpenCV 4.2 Ubuntu 包）在板上**解 ~15% 帧就永久卡死**——不是代码问题；
2. 用 **GStreamer 后端** + 装 `gstreamer1.0-libav` → 300/300 帧；
3. **硬解陷阱**：`mppvideodec` 解 1080p60 只要 0.95 ms/帧，但 **`videoconvert` 搬出 DMA-BUF 要 123 ns/px**
   （640×480→37.5 ms/帧）——**硬解只有下一环直接吃 DMA-BUF 才有收益**（00 §6 L 铁律）。

## 六、概念清单（一句话 + 搜索关键词 + 资料）

| 概念 | 一句话 | 搜索关键词 | 资料 |
|---|---|---|---|
| JPEG 解码成本 | Huffman+IDCT 是逐像素大头 | "libjpeg decode performance cycles per pixel" | 手册 §7.1；CSAPP 5 章 |
| BMP 对照法 | 换变量（压缩格式）保持其他不变 | "controlled experiment isolate variable" | 《调试与实验方法论》 |
| devfreq/governor | 频率调节器，测试前必须锁 performance | "rockchip dmc devfreq governor performance" | 00 §6 J |
| CPU 亲和性 | taskset 把线程钉在大核，消除迁移 | "taskset cpu affinity scheduling migration" | Linux man taskset |
| GStreamer pipeline | `元素 ! 元素 ! ...` 的数据流管道 | "gstreamer pipeline appsink sync=false" | GStreamer 官方文档 |
| sync=true/false | 是否按时钟节流（实时播放 vs 全速） | "appsink sync property clock" | GStreamer appsink 文档 |
| DMA-BUF | 硬件间零拷贝缓冲，CPU 访问昂贵 | "dma-buf videoconvert copy cost" | 00 §6 L |
| 元教训 | 计时点名字要诚实（"read"误导排查方向） | — | 00 §6 I |

## 七、逐行预检表（`main.cc` 循环计时块，写前别看实现）

| 块 | 行为 | ✅/❓ |
|---|---|---|
| 1. 循环取帧 `cap.read(frame)` | 失败要 break 不是 return | |
| 2. `StageStat::add` | 第一帧初始化 min/max | |
| 3. `t = getTickCount()` 配对 | 每段独立计时，用 tick 差 | |
| 4. `--fast` 构造 pipeline | `sync=false` 是关键字 | |
| 5. `--quiet` 关诊断 | 省 ≈0.3 ms/帧 | |
| 6. 汇总表 | 平均/最小/最大三列 | |
| 7. 视频提前结束警告 | GStreamer 模式 FRAME_COUNT=−1 要另判 | |

## 八、硬约定

1. 测数字前：**锁频（CPU×3+NPU+**dmc**）+ taskset 绑大核**，缺一不可；
2. 视频实验一律想清楚"是不是在被节流"；要真实吞吐用 `--fast`；
3. 计时点命名诚实；对着名字排查会走错方向（read≠IO）。
