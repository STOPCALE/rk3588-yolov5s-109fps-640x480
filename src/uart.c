// src/uart.c —— 串口底层驱动（Linux termios）
// ---------------------------------------------------------------------------
// 职责：管理 /dev/ttyS4 这类串口设备 —— 打开/配置/写/读/关闭。
// 定位：只搬字节，不管协议（二进制包怎么组、CRC 怎么算，在协议层做）。
//
// B9 设计定稿的串口配置：
//   115200 baud · 8 数据位 · 无校验 · 1 停止位 · 无流控 · 原始字节流
//   读不阻塞：没有数据时 read() 返回 0（靠 VMIN=0 / VTIME=0 实现）
//
// ⭐ 为什么每一处都要写仔细？
//   Linux 上串口默认是"终端"，不是"哑管道"：
//   驱动会做回车换行翻译、软件流控、行缓冲、回显…… 任何一条没关掉，
//   二进制数据（比如我们 0xA5 开头的包头）都会在系统层被悄悄改写 —— CRC 直接对不上。
// ---------------------------------------------------------------------------

#include "uart.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// 波特率数值 -> termios 常量（只支持 uart.h 承诺的那几档，其余直接拒绝）
// ---------------------------------------------------------------------------
static int baud_to_speed(int baudrate)
{
    switch (baudrate)
    {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:     return -1;     //不认识的档位：宁可报错，不要瞎猜
    }
}

// ---------------------------------------------------------------------------
// 打开并配置串口
// 成功返回 fd(>=0)；失败打印原因并返回 -1
// ---------------------------------------------------------------------------
int uart_open(const char *device, int baudrate)
{
    int speed = baud_to_speed(baudrate);
    if (speed < 0)
    {
        fprintf(stderr, "[uart] 不支持的波特率: %d\n", baudrate);
        return -1;
    }

    // 1) 打开设备
    //    O_RDWR   : 读、写都要
    //    O_NOCTTY : 不要把这个串口变成进程的控制终端
    //               （否则它可能把 Ctrl+C 这类信号接管走）
    int fd = open(device, O_RDWR | O_NOCTTY);
    if (fd < 0)
    {
        fprintf(stderr, "[uart] 打开 %s 失败: %s\n", device, strerror(errno));
        return -1;
    }

    // 2) 取出当前配置 —— 串口刚打开时还是"终端模式"的设置，下面逐组改掉
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0)
    {
        fprintf(stderr, "[uart] tcgetattr 失败: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    // 3) 输入旗标：关掉一切"改写/拦截字节"的处理
    //    IXON/IXOFF/IXANY : 软件流控。数据里出现 0x11/0x13 会被当流控字符吞掉
    //    INLCR/IGNCR/ICRNL: 回车换行翻译。0x0D 可能被改成 0x0A —— 二进制包大忌
    //    BRKINT/PARMRK/IGNBRK/ISTRIP : 中断处理 / 剥掉第 8 位 等杂项，全部关掉
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP |
                     INLCR  | IGNCR  | ICRNL  |
                     IXON   | IXOFF  | IXANY);

    // 4) 输出旗标：关掉输出后处理（OPOST）
    //    默认开着时，写出去 '\n' 会被翻译成 '\r''\n' —— 同样会篡改字节
    tty.c_oflag &= ~OPOST;

    // 5) 控制旗标：8N1 + 本地模式
    //    CSIZE/PARENB/CSTOPB : 数据位掩码 / 校验位 / 2 停止位 —— 全清后重设
    //    CRTSCTS             : 硬件流控（不清的话，CTS 悬空可能卡住发送）
    //    CLOCAL              : 忽略调制解调器控制线（嵌入式必设）
    //    CREAD               : 允许接收
    tty.c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS);
    tty.c_cflag |=  (CS8 | CLOCAL | CREAD);

    // 6) 本地旗标：关掉"终端行为"
    //    ICANON : 行缓冲 —— 开着的话 read 要等一整行才返回（我们要字节一到就返回）
    //    ECHO*  : 回显 —— 开着的话写出去的字节会被原样回显
    //    ISIG   : Ctrl+C/Z 等信号键（串口数据里出现 0x03 会产生信号！）
    //    IEXTEN : 扩展输入处理
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ISIG | IEXTEN);

    // 7) 波特率
    if (cfsetispeed(&tty, speed) != 0 || cfsetospeed(&tty, speed) != 0)
    {
        fprintf(stderr, "[uart] 设置波特率失败: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    // 8) 读行为参数（termios 里最容易被忽略、但决定 read() 语义的两个字节）
    //    VMIN=0, VTIME=0 -> read() 立即返回：读到多少返回多少，没有数据返回 0
    //    （对比：VMIN>0 会阻塞到读满；VTIME>0 是"按 0.1s 计时"的超时读）
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    // 9) 让上面的配置立即生效
    if (tcsetattr(fd, TCSANOW, &tty) != 0)
    {
        fprintf(stderr, "[uart] tcsetattr 失败: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    // 10) 清空收发缓冲：丢掉打开前残留的字节，避免第一次读回比对被污染
    tcflush(fd, TCIOFLUSH);

    return fd;
}

// ---------------------------------------------------------------------------
// 发送 len 字节（循环写，直到全部写完或真出错）
// 返回：实际写出的字节数；失败返回 -1
// ---------------------------------------------------------------------------
ssize_t uart_write(int fd, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    size_t done = 0;

    // write() 可能"只写一半"：内核发送缓冲满时，它先接收一部分就返回。
    // 必须接着写剩下的，否则包就断了（接收端会看到半包）。
    while (done < len)
    {
        ssize_t n = write(fd, p + done, len - done);

        if (n < 0)
        {
            if (errno == EINTR) continue;   //被信号打断：重试，不算失败
            fprintf(stderr, "[uart] write 失败: %s\n", strerror(errno));
            return -1;
        }
        done += (size_t)n;
    }
    return (ssize_t)done;
}

// ---------------------------------------------------------------------------
// 读当前可读的字节（不阻塞等待）
// 返回：>0 读到的字节数；0 暂时没有数据；-1 出错
// ---------------------------------------------------------------------------
ssize_t uart_read(int fd, void *buffer, size_t size)
{
    for (;;)
    {
        ssize_t n = read(fd, buffer, size);
        if (n < 0 && errno == EINTR) continue;  //被信号打断：重试
        return n;   // VMIN=0/VTIME=0 下，没数据时这里返回 0
    }
}

// ---------------------------------------------------------------------------
// 关闭串口
// ---------------------------------------------------------------------------
void uart_close(int fd)
{
    if (fd >= 0) close(fd);
}
