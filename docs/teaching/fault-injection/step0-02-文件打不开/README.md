# step0 故障练习 · 「文件打不开」（独立小项目）

这不是主工程，只是一个可独立编译、安装、运行的小项目。

## 构建 / 安装 / 运行（板子上或有 cmake 的环境）

```bash
cmake -B build -S .
cmake --build build -j4
cmake --install build

cd install && ./reader     # ← 现象在这里出现
```

## 可做的记录（写进工作表）

```bash
ls -la install/            # 安装目录里到底有什么？
find install -name "data.txt"   # data.txt 去哪了？
```

> 工作表在 `docs/teaching/step0-cpp-骨架/fault-injection.md`；
> 卡壳时按 `docs/teaching/step0-cpp-骨架/hints.md` 的分级卡逐步取提示。
