#include "postprocess.h"
#include <math.h>
#include <stdio.h>
#include <algorithm>

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

