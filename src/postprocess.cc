#include "postprocess.h"
#include <math.h>
#include <stdio.h>
#include <algorithm>

//检测头
static const int VB_ANCHORS[VB_HEAD_NUM][VB_ANCHOR_NUM * 2] = {
    { 10, 13, 16, 30, 33, 23 },         // 80x80
    { 30, 61, 62, 45, 59, 119 },        // 40x40
    { 116, 90, 156, 198, 373, 326 }     // 20x20
};

//反量化
static inline float deqnt(int8_t q, int32_t zp, float scale)
{
    return ((float)q - (float)zp) * scale;
}

//把阈值变成int8，这是预筛选关键
static inline int8_t qnt_f32(float f, int32_t zp, float scale)
{
    //约束值
    int v = (int)(f / scale + 0.5f) + zp;
    if (v > 127)  v = 127;
    if (v < -128) v = -128;
    return (int8_t)v;
}

//解码
int vb_decode(const vb_head_t heads[VB_HEAD_NUM], int model_w, int model_h,
                float conf_thresh, vb_result_t *result)
{
    //对结果进行清空
    result->items.clear();
    result->items.reserve(256);

    //遍历每一个检测头

    //这是什么？
    for (int h = 0; h < VB_HEAD_NUM; h++)
    {
        const int8_t *data = heads[h].data;
        const int     gw   = heads[h].grid_w;
        const int     gh   = heads[h].grid_h;
        const int     glen = gw * gh;
        const int32_t zp   = heads[h].zp;
        const float   scale= heads[h].scale;

        //stride 反推，可变
        const float stride = (float)model_w / (float)gw;

        //阈值量化
        const int8_t thres_i8 = qnt_f32(conf_thresh, zp, scale);

        //遍历每一个网格

        //对每个检测头进行遍历
        for (int a = 0; a < VB_ANCHOR_NUM; a++)
        {
            const int   base = VB_PROP_SIZE * a * glen;       //计算anchor的起点
            const float aw   = (float)VB_ANCHORS[h][a * 2 + 0];   //anchor宽
            const float ah   = (float)VB_ANCHORS[h][a * 2 + 1];   //anchor高

            //对obj进行处理

            //对高进行解码，选出一个合适的高
            for(int i = 0; i < gh; i++)
            {
                //对长进行解码，选出一个合适的长
                for (int j = 0; j < gw; j++)
                {
                    const int       idx = base + i * gw + j;   //计算网格的索引
                    const int8_t    obj = data[idx + 4 * glen];                 //取出obj

                    //整数比较
                    if (obj < thres_i8) continue;   //不通过，跳过

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

                    float bx_raw = deqnt(data[idx + 0 * glen], zp, scale)  *2.0f - 0.5f;
                    float by_raw = deqnt(data[idx + 1 * glen], zp, scale)  *2.0f - 0.5f;
                    float bw_raw = deqnt(data[idx + 2 * glen], zp, scale)  *2.0f;
                    float bh_raw = deqnt(data[idx + 3 * glen], zp, scale)  *2.0f;

                    const float cx = (bx_raw + (float)j) * stride;   //球心x
                    const float cy = (by_raw + (float)i) * stride;   //球心y
                    const float bw = (bw_raw * bw_raw * aw);                //球宽
                    const float bh = (bh_raw * bh_raw * ah);                //球高

                    //数据赋值
                    vb_box_t box;
                    box.cx     = cx;
                    box.cy     = cy;
                    box.bw     = bw;
                    box.bh     = bh;
                    box.radius = (bw + bh) * 0.25f;   //半径 = (宽+高)/4
                    box.prop   = deqnt(obj, zp, scale);   //置信度
                    box.head   = h;                   //检测头
                    box.anchor = a;                   //anchor索引

                    result->items.push_back(box);
                }

            }
        }
    }
    return (int)result->items.size();
}

//两框交并比 IoU = 交集面积 / 并集面积
//判断两个框的重叠程度
static float vb_iou(const vb_box_t &a, const vb_box_t &b)
{
    //中心+宽高，换算成四个边界
    const float ax0 = a.cx - a.bw * 0.5f, ay0 = a.cy - a.bh * 0.5f;
    const float ax1 = a.cx + a.bw * 0.5f, ay1 = a.cy + a.bh * 0.5f;
    const float bx0 = b.cx - b.bw * 0.5f, by0 = b.cy - b.bh * 0.5f;
    const float bx1 = b.cx + b.bw * 0.5f, by1 = b.cy + b.bh * 0.5f;

    //交集：两框在两个方向重叠
    const float w = fmaxf(0.0f, fminf(ax1, bx1) - fmaxf(ax0, bx0));
    const float h = fmaxf(0.0f, fminf(ay1, by1) - fmaxf(ay0, by0));
    const float inter = w * h;

    //并集 = A + B - 交集
    const float area_a = (ax1 - ax0) * (ay1 - ay0);
    const float area_b = (bx1 - bx0) * (by1 - by0);
    const float uni    = area_a + area_b - inter;

    return (uni <- 0.0f) ? 0.0f : (inter / uni);

}

//NMS抑制，用来保证最可信
int vb_nms(vb_result_t * result, float iou_thresh)
{
    std::vector<vb_box_t> &boxes = result->items;
    if (boxes.size() < 2) return (int)boxes.size();

    //按置信度降序，贪心NMS
    std::sort(boxes.begin(), boxes.end(),
                [](const vb_box_t &a, const vb_box_t &b)
                { return a.prop > b.prop;});

    //贪心抑制
    std::vector<char> dead(boxes.size(), 0);
    int keep = 0;   //下一项存活的位置

    //选则抑制位置
    for (size_t i = 0; i < boxes.size(); i++)
    {
        if (dead[i]) continue;

        boxes[keep++] = boxes[i];   //保留第i个

        //防止重叠过多
        for (size_t j = 0; j < boxes.size(); j++)
        {
            if (dead[j])    continue;
            if (vb_iou(boxes[i], boxes[j]) > iou_thresh) dead[j] = 1;
        }
    }

    boxes.resize(keep);
    return keep;
}


//step2的后处理函数
void vb_to_original(const std::vector<vb_box_t> &src, std::vector<vb_ball_t> &dst,
                    const BOX_RECT &pads, float scale)

{
    dst.clear();
    dst.reserve(src.size());

    //除法变乘法
    const float inv_scale = 1.0f/ scale;

    for (size_t i = 0; i < src.size(); ++i)
    {
        vb_ball_t ball;

        //先平移再缩放
        ball.cx     = (src[i].cx - pads.left) * inv_scale;
        ball.cy     = (src[i].cy - pads.top) * inv_scale;

        //长度，只缩放
        ball.radius = src[i].radius * inv_scale;
        ball.prop   = src[i].prop;
        dst.push_back(ball);
    }
}

