# step6 · 提示分级卡（卡壳时翻，不卡不看）

> L3 使用规则不变：翻到 L3 ＝ 当天合上资料凭记忆重做 + 登记到附录 E。

---

## L1 · 方向（一句话指路）

串口问题先分三层：**字节层**（termios：翻译/回显/流控）、**包层**（组包/CRC）、**逻辑层**（预测/门限）。
- 字节"变形/丢失" → termios；
- 整包错乱/丢弃 → 协议/CRC；
- 数值对但轨迹乱 → 选择逻辑。

## L2 · 检查点

| 症状 | 先查什么 |
|---|---|
| 某几个字节被改写（0x0D↔0x0A） | `c_iflag` 的 ICRNL/INLCR/IGNCR 清了吗？ |
| 偶发丢字节/粘字节 | IXON/IXOFF（软流控）残留？ |
| 读不到超时返回 | VMIN/VTIME 语义（0/0=非阻塞） |
| 写不全 | write 循环 + 部分写处理有吗？ |
| CRC 偶发失败 | tcflush 清残留？帧头重同步？ |
| 速度估计"突然飞了" | 连发帧 → min_dt_ms 保护 |
| 目标频繁切换 | 选择逻辑没有门限（改为"选最近"） |
| 锁到静止物 | 重锁半径/锚点机制 |

## L3 · 关键片段（翻到此级 = 合上重做 + 登记）

```c
// 裸模式（节选）：
tcgetattr(fd, &t);                 // 1. 查返回值！
t.c_lflag &= ~(ICANON | ECHO);     // 关规范模式/回显
t.c_iflag &= ~(INLCR | IGNCR | ICRNL);   // ★ 关回车翻译三兄弟
t.c_iflag &= ~(IXON | IXOFF);      // 关软件流控
t.c_cflag &= ~CRTSCTS;             // 关硬件流控
t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;   // 非阻塞语义（弃 O_NDELAY）
tcsetattr(fd, TCSANOW, &t);        // 查返回值！
tcflush(fd, TCIOFLUSH);            // 清残留

// write 循环（部分写 + EINTR）：
ssize_t off = 0;
while (off < len) {
    ssize_t n = write(fd, buf + off, len - off);
    if (n < 0) { if (errno == EINTR) continue; return -1; }
    off += n;
}

// 组包（15B，全大端）
buf[0]=0xA5;
put_i16_be(&buf[1], dx); put_i16_be(&buf[3], dy);
put_i16_be(&buf[5], vx); put_i16_be(&buf[7], vy);
put_i16_be(&buf[9], r);
buf[11]=det; buf[12]=pred;
buf[13]=crc8(buf, 13);
buf[14]=0x5A;
```

构建运行故障小项目（板子上）：

```bash
cd ~/myproj/docs/teaching/fault-injection/step6-01-回车变异
cmake -B build -S . && cmake --build build -j4 && ./build/pty_crlf
```
