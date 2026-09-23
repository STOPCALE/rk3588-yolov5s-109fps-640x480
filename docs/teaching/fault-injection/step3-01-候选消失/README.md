# step3 故障练习 · 「候选消失」（纯 C++，PC/板子都能跑）

独立小项目，无任何依赖。

## 构建运行

```bash
# 方式 A：直接编译（PC 的 MinGW / 板子的 g++ 都行）
g++ -std=c++14 -O2 cand_gone.cc -o cand
./cand            # PC 上是 ./cand.exe

# 方式 B：cmake
cmake -B build -S . && cmake --build build -j4 && ./build/cand
```

程序会打印它"扫描"到的候选数量，并附有**构造时写入的 ground truth**。
把程序当成陌生人写的来排查。

> 工作表在 `docs/teaching/step3-后处理/fault-injection.md`；
> 卡壳按 `docs/teaching/step3-后处理/hints.md` 分级取提示。
