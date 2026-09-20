// src/serial_selftest.c —— 串口底层最小验证（验证工具，不进 CMake）
// ---------------------------------------------------------------------------
// 测什么：uart_open / uart_write / uart_read / uart_close 这条最底层通路，
//         以及"字节流是否原样传输"（二进制协议的前提条件）。
// 怎么构造：写入 16 字节已知模式（0x10..0x1F），再从同一串口读回来逐字节比对。
//   · 回环测试：杜邦线短接 UART4 的 pin16(TX) <-> pin18(RX)
//   · 没插跳线时读回 0 字节 -> 打印 FAIL 并提示（属预期现象，不是 bug）
// 判据：读回 16 字节且逐字节相等 = PASS（退出码 0），否则 FAIL（退出码 1）
// 编译（在板子上，工程根目录）：
//   gcc -Wall -Wextra src/serial_selftest.c src/uart.c -Iinclude -o /tmp/serial_selftest
// 运行：
//   /tmp/serial_selftest /dev/ttyS4
// ---------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "uart.h"

int main(int argc, char **argv)
{
    const char *dev = (argc > 1) ? argv[1] : "/dev/ttyS4";

    int fd = uart_open(dev, 115200);
    if (fd < 0)
    {
        fprintf(stderr, "uart_open(%s) 失败\n", dev);
        return 1;
    }
    printf("[1] open ok: %s fd=%d\n", dev, fd);

    // 已知模式：16 个递增字节，任何一位被改写都能一眼看出来
    unsigned char tx[16];
    for (int i = 0; i < 16; i++) tx[i] = (unsigned char)(0x10 + i);

    ssize_t w = uart_write(fd, tx, sizeof tx);
    printf("[2] write: %zd/%zu 字节\n", w, sizeof tx);

    // 最多等 500ms：轮询读回（每次没数据会立即返回 0，所以用 sleep 控制节奏）
    unsigned char rx[64];
    memset(rx, 0, sizeof rx);
    ssize_t r = 0;
    for (int t = 0; t < 50 && r < (ssize_t)sizeof tx; t++)
    {
        ssize_t k = uart_read(fd, rx + r, sizeof rx - r);
        if (k > 0) r += k;
        else       usleep(10 * 1000);
    }
    printf("[3] read: %zd 字节\n", r);

    if (r != (ssize_t)sizeof tx || memcmp(tx, rx, sizeof tx) != 0)
    {
        printf("    rx =");
        for (ssize_t i = 0; i < r; i++) printf(" %02X", rx[i]);
        printf("\n    结果: LOOPBACK FAIL（检查 pin16<->pin18 跳线是否插好）\n");
        uart_close(fd);
        return 1;
    }

    printf("[4] 结果: LOOPBACK PASS\n");
    uart_close(fd);
    return 0;
}
