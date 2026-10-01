#ifndef FRAME_RESULT_HPP
#define FRAME_RESULT_HPP

#include <vector>
#include "opencv2/core/core.hpp"
#include "postprocess.h"

struct FrameResult
{
    cv::Mat image;

    //待会解封
    std::vector<vb_ball_t> balls;
};

#endif