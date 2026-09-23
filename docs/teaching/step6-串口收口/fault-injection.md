# step6 · 故障注入（1 款 · PTY 模拟串口 · 无需硬件）

> 本练习用 **PTY（伪终端）**在软件里模拟一根"串口回环线"——
> 打开一对 master/slave，slave 侧用的就是"uart_open 同款"的 termios 配置。
> 不需要接任何杜邦线。在板子（Linux）上跑。

---

## 故障 · 回车变异（二进制字节在链路里被"翻译"了）

- **坏版本**：`docs/teaching/fault-injection/step6-01-回车变异/`
- **症状描述**：程序往"回环线"写 16 个**已知字节**（里面含 `0x0D`），再读回来逐字节比对。
  结果：**统计出 1 个字节不一致**——写出的是 `0x0D`，读到的是 `0x0A`。
  其余 15 个字节完好。程序打印 `mismatch=1` 和差异位置。
- **你的目标**：定位根因（一句话）+ 修复 termios 配置 + 让 `mismatch = 0`（16/16 通过）。

### 板上构建运行

```bash
ssh board
cd ~/myproj/docs/teaching/fault-injection/step6-01-回车变异
cmake -B build -S . && cmake --build build -j4      # 或者直接 g++ -std=c++14 pty_crlf.cc -o pty_crlf -lutil
./build/pty_crlf
```

### 排查工作表（写在这里，别翻答案）

| 我的假设 | 判别实验 | 预期结果 |
|---|---|---|
|  |  |  |
|  |  |  |
|  |  |  |

---

> 参考线索：只有 `0x0D` 一个字节变了，`0x0A` 没变——想想"终端设备"对**回车**会做什么手脚，
> 以及 termios 的 `c_iflag` 里哪一位是干这个的。
> 这正是"清 INLCR/IGNCR/ICRNL"被列为硬约定的原因。
