# step0 故障练习 · 「运行变慢」（独立小项目）

这不是主工程，只是一个可独立编译的小基准。把它当作一个陌生的第三方小项目：**只根据现象排查**。

## 构建与运行（在板子上，或任何有 cmake 的环境）

```bash
cmake -B build -S .
cmake --build build -j4
./build/work          # 多跑几次，记下 elapsed 的大致范围（建议取中位数）
```

> 工作表在 `docs/teaching/step0-cpp-骨架/fault-injection.md`；
> 卡壳时按 `docs/teaching/step0-cpp-骨架/hints.md` 的分级卡逐步取提示。
