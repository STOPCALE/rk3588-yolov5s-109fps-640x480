#ifndef FRAME_RESULT_HPP
#define FRAME_RESULT_HPP

#include "opencv2/core/core.hpp"
#include "postprocess.h"

struct FrameResult
{
    cv::Mat image;
    detect_result_group_t detections;
};

#endif