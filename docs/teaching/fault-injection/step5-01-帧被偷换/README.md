# step5 故障练习 · 「帧被偷换」（纯 C++，PC/板子都能跑）

独立小项目，无 NPU 依赖。

## 构建运行

```bash
# 直接编译（PC 的 MinGW / 板子的 g++ 都行；注意 -pthread）
g++ -std=c++14 -O2 steal.cc -o steal -pthread
./steal

# 或 cmake
cmake -B build -S . && cmake --build build -j4 && ./build/steal
```

程序把每帧写入唯一编号后交给一个"假推理"的 worker，最后打印 `tag mismatch` 计数。
把它当成陌生人写的并发代码来排查。

> 工作表在 `docs/teaching/step5-多线程流水线/fault-injection.md`；
> 卡壳按 `docs/teaching/step5-多线程流水线/hints.md` 分级取提示。
