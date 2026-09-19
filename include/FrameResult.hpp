#ifndef FRAME_RESULT_HPP
#define FRAME_RESULT_HPP

#include <vector>
#include "opencv2/core/core.hpp"
#include "postprocess.h"

//一帧推理结果
struct FrameResult
{
    cv::Mat image;                  //原图浅拷贝
    std::vector<vb_ball_t> balls;   //检测到排球
};

#endif