# step5 · 多线程流水线（概念地图）

> 目标：从"线程池三个并发 bug"讲到"三线程流水线"——
> 理解 **深拷贝 / 背压 / 锁外等待** 为什么是并发正确性的三根柱子，
> 并用 Little 定律把"延迟有上界"变成可计算的账。
>
> 参考实现：`include/rknnPool.hpp`、`include/ThreadPool.hpp`、`src/main.cc`（`--pipe` 三线程结构）；
> 工具：`pool_selftest`、`bench_throughput`；数据：00 §6 C/N/O；公式：手册式 13/13.5/14/15/21.5/21.6。

---

## 一、架构全景（`--pipe` 模式）

```
采集线程 ──写──▶ [最新帧槽](覆盖语义) ──读──▶ 结果线程 ──put/get──▶ rknnPool(3 worker/3 NPU核)
                                                  │
                                                  └──写──▶ [显示槽] ──▶ 显示线程(落盘/imshow)

maxPending=3（在途帧上限）    背压：满员→拒收（丢旧帧，保延迟）
```

**为什么 3 个实例而不是 1 个加锁？** NPU 核是**干净的串行资源**：
3 实例各绑 1 核 → 吞吐 124.9 fps（00 §6 C/K）；1 个实例加锁 = 1 核串行 = 44.6 fps。

## 二、三根柱子（B8-2 的三处修复）

| # | 柱子 | 坏的写法 | 症状 | 修复后证据 |
|---|---|---|---|---|
| P0-1 | **深拷贝** | `cv::Mat` 浅拷贝入队 | worker 读像素时主线程已覆写 → **随机错乱** | T1：100 帧覆盖写，不一致 **0** |
| P0-2 | **背压拒收** | 只 pop future 不拦提交 | 任务进无界队列 → **延迟无上界**（原工程 30ms→800ms） | T2：accept=2 / reject=28 / 实际执行 **2** |
| P0-3 | **锁外等待** | 抱着队列锁等 future | 取帧线程的 `put()` 被堵死 | T3：阻塞 get 期间 put 耗时 **0.078 ms**（旧 ≈30ms） |

> §1.5 修复（ThreadPool 空队列 UB）同属柱子家族：**虚假唤醒**时若没"重新检查条件"，
> 会对着空队列 `front()`——UB。教训：**凡是 wait，醒来必须重新验证条件**。

## 三、假模型的先验账（不用 NPU 也能自测）

`pool_selftest` 用"假模型"（sleep 代替推理）验证三根柱子——**模板不实例化=不被检查**（方法论案例 17），
只有真跑一次实例化才会炸出编译错误。

## 四、流水线实测（00 §6 O，定频 + `taskset -c 4-7`）

| # | 输入 | 显示间隔 | 结果吞吐 | 延迟 avg(min~max) | 拒收 |
|---|---|---|---|---|---|
| A | 节流 30fps | 500ms | 30.0 fps | 24.43 (22.2~26.3) | 0 |
| B | `--fast` | 500ms | 101.2 fps | 25.24 (23.7~28.4) | 148 |
| C | `--fast` | 0ms | **106.3 fps** | 24.82 (23.3~26.4) | 142 |
| D | 节流 | 0ms | 30.0 fps | 24.23 (22.6~26.4) | 0 |

**四个结论**：① 延迟 ≈ 单帧推理 + ~1ms（**不是排队**）；② 显示 500ms→0ms 吞吐/延迟无可测差异（**解耦**）；
③ 拒收 = 采集 − 结果（"只处理最新"的证据）；④ 全速 106 vs worker 上限 125 —— 差额是"喂料间隙"（**留给你的账**）。

## 五、两个著名陷阱

1. **阻塞 `get()`**：循环"喂一帧→等一帧"，在途恒为 1 → 吞吐 = 单帧速率（42.5 fps，实测）。
   改 `get_try()`（非阻塞）→ 103+ fps。**并发系统里，"等结果"必须与"喂任务"解耦**。
2. **最新帧槽是覆盖语义**：不是队列！慢帧被新帧直接覆盖——这是"低延迟"的设计选择
   （丢失的是"过期的帧"，不是"还没处理的帧"）。

## 六、概念清单（一句话 + 搜索关键词 + 资料）

| 概念 | 一句话 | 搜索关键词 | 资料 |
|---|---|---|---|
| condition_variable | 条件等待/唤醒原语，必须配谓词循环 | "c++ condition_variable spurious wakeup predicate" | CSAPP 12 章；cppreference |
| unique_lock | 可中途解锁的锁包装（wait 需要它） | "unique_lock lock_guard difference" | 《C++ Concurrency in Action》 |
| packaged_task/future | 把"将来会有的结果"打包传递 | "std::packaged_task future 线程池" | 同上 |
| 有界队列/背压 | 队列满则拒绝，给延迟上界 | "bounded queue backpressure" | 手册 §6.1；Little 定律 |
| Little 定律 | $L = \lambda W$：在途数 = 到达率 × 停留时间 | "littles law latency throughput" | 手册式 13.5 |
| 深拷贝/浅拷贝 | Mat 拷贝=共享像素 vs 复制像素 | "opencv mat shallow copy clone" | OpenCV 官方 docs |
| 数据竞争 | 无同步的并发读写 = UB | "data race undefined behavior c++" | CSAPP 12.4 |
| 串行资源 | NPU 核一次只能跑一个任务 | "serial resource throughput model" | 00 §6 K：67.21=3×22.40 |
| 并行效率 | 吞吐N / (N×吞吐1) | "parallel efficiency speedup" | 手册式 21.5：93.3% |

## 七、逐行预检表（`rknnPool.hpp`）

| 块 | 行为 | ✅/❓ |
|---|---|---|
| 1. `put()` 入口检查满员 | `pending >= maxPending` → 拒收 | |
| 2. `cloneInput()` | 提交时**深拷贝**输入 | |
| 3. future 入队 + pending++ | 锁保护 | |
| 4. `get()` 把 future 移出 | **锁外**再 `future.get()` | |
| 5. worker 循环 | wait 谓词循环，醒来重查 | |
| 6. quit 路径 | 唤醒所有并 join | |
| 7. `get_try()` | 非阻塞版本（喂料循环用） | |

## 八、硬约定

1. **wait 必须带谓词**（醒来重新检查）；2. 输入进池必**深拷贝**；
3. 延迟要有**上界**：队列必须**有界**；4. 喂料与取结果**解耦**（try，不阻塞）。
