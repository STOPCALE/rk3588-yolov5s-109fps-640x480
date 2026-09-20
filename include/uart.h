#ifndef UART_H
#define UART_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 串口底层（Linux termios）—— 只搬字节，不管协议
 * 实现见 src/uart.c；典型用法：
 *
 *     int fd = uart_open("/dev/ttyS4", 115200);
 *     if (fd >= 0) { uart_write(fd, pkt, 15); ... uart_close(fd); }
 *
 * 配置：8 数据位 / 无校验 / 1 停止位 / 无流控 / 原始字节流
 * 读语义：不阻塞 —— 没有数据时 uart_read() 返回 0
 */

/**
 * @brief 打开并配置串口
 * @param device   设备路径，如 "/dev/ttyS4"
 * @param baudrate 波特率，支持 9600 / 19200 / 38400 / 57600 / 115200
 * @return 成功返回文件描述符（>=0），失败返回 -1
 */
int uart_open(const char *device, int baudrate);

/**
 * @brief 通过串口发送数据（内部循环处理"部分写"，保证写满或报错）
 * @param fd   文件描述符
 * @param data 数据缓冲区
 * @param len  数据长度（字节）
 * @return 成功返回写出的字节数，失败返回 -1
 */
ssize_t uart_write(int fd, const void *data, size_t len);

/**
 * @brief 从串口读取当前可读的数据（不阻塞：没有数据返回 0）
 * @param fd     文件描述符
 * @param buffer 接收缓冲区
 * @param size   缓冲区大小
 * @return >0 读到的字节数；0 暂时没有数据；-1 出错
 */
ssize_t uart_read(int fd, void *buffer, size_t size);

/**
 * @brief 关闭串口
 * @param fd 文件描述符
 */
void uart_close(int fd);

#ifdef __cplusplus
}
#endif

#endif /* UART_H */
