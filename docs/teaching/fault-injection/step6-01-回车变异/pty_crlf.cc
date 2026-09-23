// =============================================================================
//  step6 故障注入练习（坏版本）：PTY 回环 + termios 配置 + 16 字节逐字节比对。
//  构建/运行方法见同目录 README.md（板子/Linux 上跑，无需硬件）。
//  现象与工作表：docs/teaching/step6-串口收口/fault-injection.md
// =============================================================================
#include <pty.h>
#include <termios.h>
#include <unistd.h>
#include <cstdio>
#include <cstdint>
#include <cstring>

// "uart_open 同款"的 termios 配置（节选）
static int config_slave(int fd)
{
    struct termios t;
    if (tcgetattr(fd, &t) != 0) { perror("tcgetattr"); return -1; }

    t.c_lflag &= ~(ICANON | ECHO | ECHOE);   // 关规范模式 / 回显
    t.c_iflag &= ~(IXON | IXOFF);            // 关软件流控
    // （翻译位 INLCR / IGNCR / ICRNL 的清理写在这里）

    t.c_cflag |= (CLOCAL | CREAD);
    cfsetispeed(&t, B115200);
    cfsetospeed(&t, B115200);
    t.c_cc[VMIN]  = 0;                       // 非阻塞语义
    t.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &t) != 0) { perror("tcsetattr"); return -1; }
    tcflush(fd, TCIOFLUSH);
    return 0;
}

int main()
{
    int mfd = -1, sfd = -1;
    if (openpty(&mfd, &sfd, nullptr, nullptr, nullptr) != 0)
    {
        perror("openpty");
        return 1;
    }
    if (config_slave(sfd) != 0) return 1;

    // 已知 16 字节模式（含 0x0D 回车、0x0A 换行、以及类协议包的头尾/CRC 位置）
    const uint8_t out[16] = {
        0xA5, 0x00, 0x50, 0xFF, 0xD8, 0x0D, 0x55, 0xFF,
        0xD6, 0x00, 0x1E, 0x01, 0x01, 0x85, 0x5A, 0x0A
    };

    // 模拟"对端发来的字节"：写向 master → 从 slave 读出
    ssize_t nw = write(mfd, out, sizeof out);
    if (nw != (ssize_t)sizeof out) { printf("write to master failed: %zd\n", nw); return 1; }

    uint8_t in[32] = {0};
    ssize_t nr = 0;
    for (int tries = 0; tries < 200 && nr < (ssize_t)sizeof out; tries++)
    {
        ssize_t n = read(sfd, in + nr, sizeof out - nr);
        if (n > 0) nr += n;
        else usleep(2000);
    }

    printf("sent %zd bytes, got %zd bytes\n", (ssize_t)sizeof out, nr);

    int mismatch = 0, first = -1;
    for (ssize_t i = 0; i < nr && i < (ssize_t)sizeof out; i++)
    {
        if (in[i] != out[i])
        {
            if (first < 0) first = (int)i;
            mismatch++;
        }
    }

    if (nr != (ssize_t)sizeof out)
        printf("!! length mismatch (sent 16, got %zd)\n", nr);

    if (mismatch > 0)
        printf("mismatch=%d  first diff at [%d]: wrote 0x%02X  read 0x%02X\n",
               mismatch, first,
               (unsigned)out[first], (unsigned)in[first]);
    else if (nr == (ssize_t)sizeof out)
        printf("LOOPBACK PASS (16/16 bytes identical)\n");

    return mismatch ? 1 : 0;
}
