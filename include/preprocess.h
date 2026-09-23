#ifndef _RKNN_YOLOV5_DEMO_PREPROCESS_H_
#define _RKNN_YOLOV5_DEMO_PREPROCESS_H_

#include <stdio.h>
#include "im2d.h"
#include "rga.h"
#include "opencv2/core/core.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"
#include "postprocess.h"

//-----------------------------------------------------------------------------
// 【教学化改造注】本文件是原工程复制件，当前工程未使用：
//   · letterbox() 的实现在 src/rkYolov5s.cc 内（static 函数），签名与此处不同；
//   · resize_rga() 从未实现。
//   保留作对照参考 —— 属"待重写范围"，不要照此文件的声明理解现状。
//-----------------------------------------------------------------------------
void letterbox(const cv::Mat &image, cv::Mat &padded_image, BOX_RECT &pads, const float scale, const cv::Size &target_size, const cv::Scalar &pad_color = cv::Scalar(128, 128, 128));

int resize_rga(rga_buffer_t &src, rga_buffer_t &dst, const cv::Mat &image, cv::Mat &resized_image, const cv::Size &target_size);

#endif //_RKNN_YOLOV5_DEMO_PREPROCESS_H_
