// src/proto_selftest.cc —— 协议层自测（验证工具，不进 CMake）
// ---------------------------------------------------------------------------
// 测什么：
//   ① 结构字节：已知输入 -> 逐字节对照"手算期望值"（帧头/大端/负数补码/帧尾）
//   ② CRC 双实现对照：把原工程里的逐位 CRC 原样抄一份进来（crc8_orig），
//      对我们的 proto_crc8 做 200 组随机数据 + 标准检验串的等值比对
//      （两套独立实现结果一致 -> 我们的重写没有改变协议行为）
//   ③ 整包一致性：包内 crc 字段 == 对前 13 字节单独重算的 CRC
// 判据：全部通过打印 "全部 PASS"（退出码 0）
//
// 编译（纯 CPU 无依赖，PC / 板子均可）：
//   g++ -Wall -Wextra -std=c++14 -Iinclude src/proto_selftest.cc -o /tmp/proto_selftest
// 运行：
//   /tmp/proto_selftest
// ---------------------------------------------------------------------------

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "serial_proto.hpp"

// —— 原工程的 CRC 实现（原样抄写，仅改名，用作对照基准） ——
static uint8_t crc8_orig(const uint8_t *data, uint16_t length)
{
    uint8_t crc = 0xFF;  // 初始值
    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x31;  // 多项式 x^8 + x^5 + x^4 + 1
            else
                crc <<= 1;
        }
    }
    return crc;
}

static int fails = 0;

static void check(bool ok, const char *name)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) fails++;
}

static void dump_hex(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) printf(" %02X", p[i]);
    printf("\n");
}

int main()
{
    TargetPacket p;
    const uint8_t *b = reinterpret_cast<const uint8_t *>(&p);

    // ---- T1：手算对照（dx=+80, dy=-40, pdx=+85, pdy=-42, r=30, 都有效） ----
    printf("[T1] 结构字节对照 dx=80 dy=-40 pdx=85 pdy=-42 r=30 det=1 pred=1\n");
    proto_build(p, 80, -40, 85, -42, 30, true, true);
    printf("       包内容:"); dump_hex(b, sizeof p);
    check(b[0] == 0xA5, "header == A5");
    check(b[1] == 0x00 && b[2] == 0x50, "detect_dx == 00 50  (+80 = 0x0050)");
    check(b[3] == 0xFF && b[4] == 0xD8, "detect_dy == FF D8  (-40 = 0xFFD8)");
    check(b[5] == 0x00 && b[6] == 0x55, "predict_dx == 00 55 (+85 = 0x0055)");
    check(b[7] == 0xFF && b[8] == 0xD6, "predict_dy == FF D6 (-42 = 0xFFD6)");
    check(b[9] == 0x00 && b[10] == 0x1E, "radius == 00 1E   (30 = 0x001E)");
    check(b[11] == 1 && b[12] == 1, "两个 valid 标志 == 1");
    check(b[14] == 0x5A, "trailer == 5A");
    check(b[13] == proto_crc8(b, 13), "包内 crc == 前 13 字节重算");

    // ---- T2：无检测包（全 0 + 标志 0） ----
    printf("[T2] 无检测包（全 0）\n");
    proto_build(p, 0, 0, 0, 0, 0, false, false);
    printf("       包内容:"); dump_hex(b, sizeof p);
    check(b[11] == 0 && b[12] == 0, "两个 valid 标志 == 0");
    check(b[13] == proto_crc8(b, 13), "包内 crc == 前 13 字节重算");

    // ---- T3：边界值（int16 两端 + uint16 上限） ----
    printf("[T3] 边界值 dx=-32768 dy=32767 pdx=-1 pdy=1 r=65535\n");
    proto_build(p, (int16_t)-32768, (int16_t)32767, -1, 1, 65535, true, true);
    printf("       包内容:"); dump_hex(b, sizeof p);
    check(b[1] == 0x80 && b[2] == 0x00, "-32768 -> 80 00");
    check(b[3] == 0x7F && b[4] == 0xFF, "+32767 -> 7F FF");
    check(b[5] == 0xFF && b[6] == 0xFF, "-1 -> FF FF");
    check(b[9] == 0xFF && b[10] == 0xFF, "65535 -> FF FF");

    // ---- T4：CRC 双实现对照（200 组随机数据 + 标准检验串） ----
    printf("[T4] CRC 双实现对照（原工程逐位版 vs proto_crc8）\n");
    int crc_fail = 0;
    uint32_t seed = 12345;
    for (int t = 0; t < 200; t++)
    {
        uint8_t buf[64];
        size_t len = (size_t)(seed % 65);            // 长度覆盖 0..64
        for (size_t i = 0; i < len; i++)
        {
            seed = seed * 1103515245u + 12345u;       // 简单的确定性伪随机
            buf[i] = (uint8_t)(seed >> 16);
        }
        uint8_t a = proto_crc8(buf, len);
        uint8_t c = crc8_orig(buf, (uint16_t)len);
        if (a != c)
        {
            crc_fail++;
            printf("      不一致: len=%u ours=%02X orig=%02X\n", (unsigned)len, a, c);
        }
    }
    check(crc_fail == 0, "200 组随机数据 CRC 完全一致");

    const uint8_t tv[] = "123456789";                // CRC 术语中的标准检验串
    printf("      固定串 \"123456789\" -> ours=%02X orig=%02X\n",
           proto_crc8(tv, 9), crc8_orig(tv, 9));
    check(proto_crc8(tv, 9) == crc8_orig(tv, 9), "标准检验串 CRC 一致");

    // ---- 汇总 ----
    if (fails == 0) printf("\n全部 PASS\n");
    else            printf("\n有 %d 项 FAIL\n", fails);
    return fails == 0 ? 0 : 1;
}
