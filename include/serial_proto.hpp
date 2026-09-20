#ifndef SERIAL_PROTO_HPP
#define SERIAL_PROTO_HPP

// ===========================================================================
// 串口协议：15 字节二进制包（视觉 -> 下位机）
// ---------------------------------------------------------------------------
// 与"原工程"布局保持一致，唯一改动：
//   原 [9-10] 是 area(w*h 外接框面积，会饱和)  ->  本工程改成 radius(球半径，像素)
//
// 字节布局（所有多字节字段都是"高字节先"的大端）：
//   [0]     header        0xA5
//   [1-2]   detect_dx     int16  球心x - 图像中心x（正 = 球在中心右侧）
//   [3-4]   detect_dy     int16  球心y - 图像中心y
//   [5-6]   predict_dx    int16  预测(延迟补偿后)球心x - 图像中心x
//   [7-8]   predict_dy    int16
//   [9-10]  radius        uint16 球半径（像素）        <- 本工程唯一协议改动
//   [11]    detect_valid  uint8  1=本帧检测有效
//   [12]    predict_valid uint8  1=预测有效
//   [13]    crc           uint8  CRC-8（覆盖 [0]..[12] 共 13 字节）
//   [14]    trailer       0x5A
// ===========================================================================

#include <stddef.h>
#include <stdint.h>

// ---------------- 协议配置区（要改协议，只动这一段） ----------------
static const uint8_t PROTO_HEADER    = 0xA5;
static const uint8_t PROTO_TRAILER   = 0x5A;
static const size_t  PROTO_PKT_LEN   = 15;   // 整包长度
static const size_t  PROTO_CRC_COVER = 13;   // CRC 覆盖前 13 字节（[0]..[12]）

#pragma pack(push, 1)
struct TargetPacket
{
    uint8_t header;                       // [0]
    uint8_t detect_dx_high,  detect_dx_low;   // [1-2]
    uint8_t detect_dy_high,  detect_dy_low;   // [3-4]
    uint8_t predict_dx_high, predict_dx_low;  // [5-6]
    uint8_t predict_dy_high, predict_dy_low;  // [7-8]
    uint8_t radius_high, radius_low;          // [9-10]  <- 原工程是 area_high/low
    uint8_t detect_valid;                 // [11]
    uint8_t predict_valid;                // [12]
    uint8_t crc;                          // [13]
    uint8_t trailer;                      // [14]
};
#pragma pack(pop)

// "防手滑"编译期检查：结构一旦被改错（长度/字段位置变了），编译直接失败
static_assert(sizeof(TargetPacket) == PROTO_PKT_LEN,   "协议包必须正好 15 字节（检查 #pragma pack / 字段）");
static_assert(offsetof(TargetPacket, crc) == 13,       "crc 必须在第 13 字节");
static_assert(offsetof(TargetPacket, trailer) == 14,   "trailer 必须在第 14 字节");

// ---------------- CRC-8（与原工程逐位一致） ----------------
// 算法（逐位实现）：
//   crc 初值 0xFF；每个字节先"异或"进 crc；
//   然后 8 次：看 crc 最高位，1 就左移并异或多项式 0x31（x^8 + x^5 + x^4 + 1），
//   0 就只左移。
// 说明：查表法只是把这 8 次循环预先算成 256 项表，结果与本实现完全一致。
inline uint8_t proto_crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
        {
            if (crc & 0x80) crc = (crc << 1) ^ 0x31;
            else            crc = (crc << 1);
        }
    }
    return crc;
}

// ---------------- 大端拆分 ----------------
// 先转无符号再拆：补码位模式直接就能拆，负数不需要特殊处理
// （比"对有符号数右移"更干净 —— 对有符号负数右移是"实现定义行为"）
inline void proto_put_i16(uint8_t &high, uint8_t &low, int16_t v)
{
    const uint16_t u = (uint16_t)v;
    high = (uint8_t)(u >> 8);
    low  = (uint8_t)(u & 0xFF);
}
inline void proto_put_u16(uint8_t &high, uint8_t &low, uint16_t v)
{
    high = (uint8_t)(v >> 8);
    low  = (uint8_t)(v & 0xFF);
}

// ---------------- 组装完整包（填字段 + 算 CRC + 帧头帧尾） ----------------
// 注意分工：本函数只负责"字节怎么摆"。偏差值怎么算（球心-图像中心）、
// 预测值怎么来，都是调用方（main.cc / 预测器）的事。
inline void proto_build(TargetPacket &p,
                        int16_t detect_dx,  int16_t detect_dy,
                        int16_t predict_dx, int16_t predict_dy,
                        uint16_t radius,
                        bool detect_ok, bool predict_ok)
{
    p.header  = PROTO_HEADER;
    p.trailer = PROTO_TRAILER;

    proto_put_i16(p.detect_dx_high,  p.detect_dx_low,  detect_dx);
    proto_put_i16(p.detect_dy_high,  p.detect_dy_low,  detect_dy);
    proto_put_i16(p.predict_dx_high, p.predict_dx_low, predict_dx);
    proto_put_i16(p.predict_dy_high, p.predict_dy_low, predict_dy);
    proto_put_u16(p.radius_high,     p.radius_low,     radius);

    p.detect_valid  = detect_ok  ? 1 : 0;
    p.predict_valid = predict_ok ? 1 : 0;

    p.crc = proto_crc8(reinterpret_cast<const uint8_t *>(&p), PROTO_CRC_COVER);
}

#endif // SERIAL_PROTO_HPP
