# step6 故障练习 · 「回车变异」（PTY 模拟串口 · 板子上跑，无需硬件）

独立小项目：用 **PTY（伪终端）**在软件里模拟一根"串口回环线"——
写向 master 的字节会出现在 slave 的输入方向，正好复现"对端发来数据"的场景。

## 板上构建运行

```bash
ssh board
cd ~/myproj/docs/teaching/fault-injection/step6-01-回车变异

# 方式 A：直接编译（需要 -lutil）
g++ -std=c++14 -O2 pty_crlf.cc -o pty_crlf -lutil && ./pty_crlf

# 方式 B：cmake
cmake -B build -S . && cmake --build build -j4 && ./build/pty_crlf
```

程序发送 16 个已知字节并读回、逐字节比对，打印差异统计。
把它当成陌生人写的串口初始化代码来排查。

> 工作表在 `docs/teaching/step6-串口收口/fault-injection.md`；
> 卡壳按 `docs/teaching/step6-串口收口/hints.md` 分级取提示。
