#include "postprocess.h"
#include <math.h>
#include <stdio.h>
#include <algorithm>

//三个检测头的 anchor
//====================================================================
// 【沿用自旧工程 / YOLOv5 官方】这组数字的含义：
//   - 每行 3 个 anchor, 每个 anchor 是一对 (宽, 高)
//   - 单位 = 模型输入(640x640)下的【像素】
//   - 它们是 YOLOv5 官方在 COCO 上「聚类」出来的典型目标尺寸
//   - 分工: stride 8  头负责小目标 (anchor 10~33 px)
//           stride 32 头负责大目标 (anchor 116~373 px)
//   - 注意解码时会做 bw = bw_raw^2 * anchor_w,
//     所以实际框宽范围是 anchor 的 0 ~ 4 倍
// ⚠️ best.rknn 是自定义训练的单类模型。若训练时开了 autoanchor 重算过,
//    这组标准值就不匹配 → 表现为「框大小系统性偏大/偏小」
//    B7-1 的实测输出(r 是否接近手算值)会告诉我们答案
//====================================================================
static const int VB_ANCHORS[VB_HEAD_NUM][VB_ANCHOR_NUM * 2] = {
    { 10, 13, 16, 30, 33, 23},          //stride 8  -> 80x80
    { 30, 61, 62, 45, 59, 119},         //stride 16 -> 40x40
    { 116, 90, 156, 198, 373, 326},     //stride 32 -> 20x20
};

//int8 反量化：real = (q - zp) * scale
static inline float deqnt(int8_t q, int32_t zp, float scale)
{
    return ((float)q - (float)zp) * scale;
}

//float 正能化：把阈值变成int8，这是整数预筛关键
static inline int8_t qnt_f32(float f, int32_t zp, float scale)
{
    int v= (int)(f / scale + 0.5f) + zp;
    if (v > 127)  v = 127;
    if (v < -128) v = -128;
    return (int8_t)v;
}

int vb_decode(const vb_head_t heads[VB_HEAD_NUM], int model_w, int model_h,
                float conf_thresh, vb_result_t *result)
{
    result->items.clear();
    result->items.reserve(256);

    for (int h = 0; h < VB_HEAD_NUM; h++)
    {
        const int8_t *data  = heads[h].data;
        const int     gw    = heads[h].grid_w;
        const int     gh    = heads[h].grid_h;
        const int     glen  = gw * gh;
        const int32_t zp    = heads[h].zp;
        const float   scale = heads[h].scale;

        //stride 有模型宽/网格宽反推，不写死
        const float stride = (float)model_w / (float)gw;

        //阈值量化一次，后面全程整数比较
        const int8_t thres_i8 = qnt_f32(conf_thresh, zp, scale);

        for (int a = 0; a < VB_ANCHOR_NUM; a++)
        {
            const int    base = VB_PROP_SIZE * a *glen;      //anchor a的 plane 0起点
            const float  aw   = (float)VB_ANCHORS[h][a * 2 + 0];
            const float  ah   = (float)VB_ANCHORS[h][a * 2 + 1];

            for (int i = 0; i < gh; i++)
            {
                for (int j = 0; j < gw; j++)
                {
                    const int       idx = base + i * gw + j;
                    const int8_t    obj = data[idx + 4 * glen]; //obj置信度再第四个plane

                    //整数比较：用来筛99%的格子
                    if (obj < thres_i8) continue;

                    //通过筛选才做浮点解码
                    //---------------------------------------------------------------
                    // 【解码公式，沿用自旧工程 process() / RKNN 官方 demo】
                    //   1) deqnt:     int8 → real。输出已过 sigmoid, 所以 real ∈ [0,1]
                    //   2) *2.0-0.5:  [0,1] → [-0.5, 1.5]  ← YOLOv5 的「网格内偏移范围」
                    //                 (中心点可以超出所属格子最多半格)
                    //   3) *2.0 再平方: [0,1] → [0,4]     ← 「尺度系数」
                    //                 0.25 是典型值(对应 anchor 的 1 倍)
                    //   4) + j / + i: 加上格子索引(列/行)
                    //   5) * stride:  格子坐标 → 模型输入像素坐标
                    //   6) * anchor:  尺度系数 × 典型目标尺寸 = 实际框尺寸
                    //---------------------------------------------------------------
                    float bx_raw = deqnt(data[idx + 0 * glen], zp, scale) * 2.0f - 0.5f;
                    float by_raw = deqnt(data[idx + 1 * glen], zp, scale) * 2.0f - 0.5f;
                    float bw_raw = deqnt(data[idx + 2 * glen], zp, scale) * 2.0f;
                    float bh_raw = deqnt(data[idx + 3 * glen], zp, scale) * 2.0f;

                    const float cx = (bx_raw + (float)j) * stride;  //球心x
                    const float cy = (by_raw + (float)i) * stride;  //球心y
                    const float bw = bw_raw * bw_raw * aw;          //球宽（像素）
                    const float bh = bh_raw * bh_raw * ah;          //球高（像素）

                    vb_box_t box;
                    box.cx      = cx;
                    box.cy      = cy;
                    box.bw      = bw;
                    box.bh      = bh;
                    box.radius  = (bw + bh) * 0.25f; //半径 = （宽+高）/4
                    box.prop    = deqnt(obj, zp, scale);
                    box.head    = h;
                    box.anchor  = a;
                    result->items.push_back(box);
                }
            }
        }
    }
    return (int)result->items.size();
}

//两框的交并比 IoU = 交集面积 / 并集面积
//------------------------------------------------------------
// 【沿用自旧工程 CalculateOverlap()，有一处【故意的改动】】
//   旧工程写:  w = fmax(0, fmin(xmax0,xmax1) - fmax(xmin0,xmin1) + 1.0)
//                                                             ^^^^^
//   那个 "+1.0" 是【整数像素索引】坐标系下的约定 ——
//   像素是"格子"不是"点"，从第 0 列到第 0 列的框宽度算 1 个像素。
//
//   但我们的坐标是【连续浮点】(模型解码出来的)，
//   框占据的区间长度就是 (xmax - xmin)，所以【不加 1.0】。
//
//   ⚠️ 这不是"坐标系对比"，而是"那个 +1 属于哪种坐标系"的问题。
//      对 23px 的框，加 1 会让宽度多 4%、IoU 多约 8% —— 本例不影响结果，
//      但写对坐标系比"抄得像"更重要。
//------------------------------------------------------------
static float vb_iou(const vb_box_t &a, const vb_box_t &b)
{
    //把"中心 + 宽高"换算成四个边界
    const float ax0 = a.cx - a.bw * 0.5f, ay0 = a.cy - a.bh * 0.5f;
    const float ax1 = a.cx + a.bw * 0.5f, ay1 = a.cy + a.bh * 0.5f;
    const float bx0 = b.cx - b.bw * 0.5f, by0 = b.cy - b.bh * 0.5f;
    const float bx1 = b.cx + b.bw * 0.5f, by1 = b.cy + b.bh * 0.5f;

    //交集：两框在两个方向上的重叠长度，负值截断为 0
    const float w = fmaxf(0.0f, fminf(ax1, bx1) - fmaxf(ax0, bx0));
    const float h = fmaxf(0.0f, fminf(ay1, by1) - fmaxf(ay0, by0));
    const float inter = w * h;

    //并集 = 面积A + 面积B - 交集
    const float area_a = (ax1 - ax0) * (ay1 - ay0);
    const float area_b = (bx1 - bx0) * (by1 - by0);
    const float uni    = area_a + area_b - inter;

    return (uni <= 0.0f) ? 0.0f : (inter / uni);
}

//NMS抑制，把同一个球产生的多个重复目标合成一个
int vb_nms(vb_result_t *result, float iou_thresh)
{
    std::vector<vb_box_t> &boxes = result->items;
    if (boxes.size() < 2) return (int)boxes.size();

    //按置信度降序 --贪心NMS，从最可信开始
    std::sort(boxes.begin(), boxes.end(),
                [](const vb_box_t &a, const vb_box_t &b)
    { return a.prop > b.prop; });

    //贪心抑制
    std::vector<char> dead(boxes.size(), 0);
    int keep = 0;   //下一个存活项要写的位置

    for (size_t i = 0; i < boxes.size(); i++)
    {
        if (dead[i]) continue;

        boxes[keep++] = boxes[i];   //保留第i个

        //抑制后面所哟与其重叠过多的
        for (size_t j = 0; j < boxes.size(); j++)
        {
            if (dead[j]) continue;
            if (vb_iou(boxes[i], boxes[j]) > iou_thresh) dead[j] = 1;
        }
    }

    boxes.resize(keep);
    return keep;
}

void vb_to_original(const std::vector<vb_box_t> &src, std::vector<vb_ball_t> &dst,
                        const BOX_RECT &pads, float scale)
{
    dst.clear();
    dst.reserve(src.size());

    //除法变乘法：只算一次倒数，循环里都是乘法
    const float inv_scale = 1.0f / scale;

    for (size_t i = 0; i < src.size(); i++)
    {
        vb_ball_t ball;
        //位置：先平移再缩放
        ball.cx     = (src[i].cx - (float)pads.left) * inv_scale;
        ball.cy     = (src[i].cy - (float)pads.top)  * inv_scale;
        //长度：只缩放
        ball.radius = src[i].radius * inv_scale;
        ball.prop   = src[i].prop;
        dst.push_back(ball);
    }
}


