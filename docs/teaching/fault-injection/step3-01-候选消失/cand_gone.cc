// =============================================================================
//  step3 故障注入练习（坏版本）：合成一块输出张量 + 整数预筛 + 解码。
//  构建/运行方法见同目录 README.md（纯 C++，PC/板子均可）。
//  现象与工作表：docs/teaching/step3-后处理/fault-injection.md
// =============================================================================
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <vector>

// ---- 与工程一致的口径（best.rknn / 头0）----
static const int   W = 80, H = 80;            // 头0网格
static const int   G = W * H;                 // 6400
static const int32_t ZP = -128;
static const float   SCALE = 0.003922f;       // ≈1/255
static const int   ANCHOR_W = 10, ANCHOR_H = 13;   // 头0 anchor0

// 植入位置（已知"球"在哪里）：
static const int PLANT_A = 0, PLANT_I = 30, PLANT_J = 38;
// 植入值的 int8 编码（由 σ 目标值反推：q = round(σ / SCALE) - 128）
static const int PLANT_TX = 47;   // σ≈0.686 → cx≈311
static const int PLANT_TY = 95;   // σ≈0.875 → cy≈250
static const int PLANT_TW = 54;   // σ≈0.714 → r≈11.7
static const int PLANT_TH = 54;
static const int PLANT_OBJ = 112; // 0.941
static const int PLANT_CLS = 127; // 1.000

// -----------------------------------------------------------------------------
//  取字段：按"本程序对内存布局的理解"计算下标
// -----------------------------------------------------------------------------
static inline int field_idx(int a, int f, int i, int j)
{
    return (i * W + j) * 18 + 6 * a + f;   // 布局的"理解"写在这一行
}

static inline float deqnt(int8_t q)
{
    return ((float)q - (float)ZP) * SCALE;
}

// 扫描 + 预筛 + 解码（单头）
static int scan(const std::vector<int8_t> &buf, float conf_thresh, bool print_boxes)
{
    // 阈值量化一次
    int tv = (int)(conf_thresh / SCALE + 0.5f) + ZP;
    if (tv > 127) tv = 127;
    if (tv < -128) tv = -128;

    const float stride = 640.0f / (float)W;
    int found = 0;

    for (int a = 0; a < 3; a++)
    {
        for (int i = 0; i < H; i++)
        {
            for (int j = 0; j < W; j++)
            {
                int8_t obj = buf[field_idx(a, 4, i, j)];
                if (obj < (int8_t)tv) continue;

                float sx = deqnt(buf[field_idx(a, 0, i, j)]);
                float sy = deqnt(buf[field_idx(a, 1, i, j)]);
                float sw = deqnt(buf[field_idx(a, 2, i, j)]);
                float sh = deqnt(buf[field_idx(a, 3, i, j)]);
                float sc = deqnt(buf[field_idx(a, 5, i, j)]);

                float cx = (sx * 2.0f - 0.5f + (float)j) * stride;
                float cy = (sy * 2.0f - 0.5f + (float)i) * stride;
                float bw = (sw * 2.0f) * (sw * 2.0f) * (float)ANCHOR_W;
                float bh = (sh * 2.0f) * (sh * 2.0f) * (float)ANCHOR_H;
                float r  = (bw + bh) * 0.25f;
                float prop = deqnt(obj) * sc;

                found++;
                if (print_boxes)
                    printf("  cand: a=%d i=%d j=%d  cx=%.1f cy=%.1f r=%.1f prop=%.3f\n",
                           a, i, j, cx, cy, r, prop);
            }
        }
    }
    return found;
}

int main()
{
    // 构造合成张量：全 -128（真实值 0），再植入一个"球"
    std::vector<int8_t> buf((size_t)18 * G, -128);

    // 注意：植入方按【文档布局】（手册 §1.2）写入 —— 这是"张量生产者"的责任，写的是对的
    auto put = [&](int a, int f, int i, int j, int v) {
        buf[(size_t)((6 * a + f) * G + i * W + j)] = (int8_t)v;
    };
    put(PLANT_A, 0, PLANT_I, PLANT_J, PLANT_TX);
    put(PLANT_A, 1, PLANT_I, PLANT_J, PLANT_TY);
    put(PLANT_A, 2, PLANT_I, PLANT_J, PLANT_TW);
    put(PLANT_A, 3, PLANT_I, PLANT_J, PLANT_TH);
    put(PLANT_A, 4, PLANT_I, PLANT_J, PLANT_OBJ);
    put(PLANT_A, 5, PLANT_I, PLANT_J, PLANT_CLS);

    printf("synthetic tensor: %zu bytes, planted 1 ball at (a=%d, i=%d, j=%d)\n",
           buf.size(), PLANT_A, PLANT_I, PLANT_J);
    printf("ground truth (by construction): 1 candidate @ (311.0, 250.0) r~11.7\n\n");

    int n = scan(buf, 0.25f, true);
    printf("\ncandidates found = %d\n", n);
    if (n != 1)
        printf("!! ground truth says there IS exactly one ball — dig into the indexing !!\n");
    return 0;
}
