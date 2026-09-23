# step5 故障练习 · 「流水线串行化」（纯 C++，PC/板子都能跑）

独立小项目，无 NPU 依赖。

## 构建运行

```bash
# 直接编译（PC 的 MinGW / 板子的 g++ 都行；注意 -pthread）
g++ -std=c++14 -O2 pipeline.cc -o pipeline -pthread
./pipeline        # 全程约 13 秒（两个阶段各跑一遍）

# 或 cmake
cmake -B build -S . && cmake --build build -j4 && ./build/pipeline
```

程序用 3 个 worker（每个"推理"假睡 24ms）跑同一批任务，打印两种节奏下的吞吐。
把它当成陌生人写的并发代码来排查。

> 数字随调度器略有差异（实测：**板子 41.5 / 124.1 fps；PC 32 / 96 fps**）——
> 关键看"A 与 B 的台阶"本身，不要背数字。

> 工作表在 `docs/teaching/step5-多线程流水线/fault-injection.md`；
> 卡壳按 `docs/teaching/step5-多线程流水线/hints.md` 分级取提示。
