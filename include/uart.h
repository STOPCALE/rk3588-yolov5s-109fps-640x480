#ifndef UART_H
#define UART_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 打开并配置串口设备
 * @param device 设备路径，如 "/dev/ttyS4"
 * @param baudrate 波特率，支持 9600, 19200, 38400, 57600, 115200
 * @return 成功返回文件描述符，失败返回 -1
 */
int uart_open(const char *device, int baudrate);

/**
 * @brief 通过串口发送数据
 * @param fd 文件描述符
 * @param data 数据缓冲区
 * @param len 数据长度
 * @return 成功返回写入字节数，失败返回 -1
 */
ssize_t uart_write(int fd, const void *data, size_t len);

/**
 * @brief 从串口读取数据（非阻塞）
 * @param fd 文件描述符
 * @param buffer 接收缓冲区
 * @param size 缓冲区大小
 * @return 成功返回读取字节数，0 表示无数据，-1 表示错误
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