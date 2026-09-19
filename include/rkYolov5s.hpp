#ifndef RKYOLOV5S_HPP
#define RKYOLOV5S_HPP

#include <string>
#include <mutex>
#include <vector>
#include "rknn_api.h"
#include "opencv2/core/core.hpp"
#include "FrameResult.hpp"

class rkYolov5s
{
public:
    explicit rkYolov5s(const std::string& model_path);
    ~rkYolov5s();

    //share_weight=false 在init时
    //share_weight=true 在dup_context时
    int init(rknn_context *ctx_in = nullptr, bool share_weight = false);

    //需要其做dup
    rknn_context *get_pctx();

    FrameResult infer(cv::Mat &orig_img);

private:
    std::string model_path;
    float nms_threshold = 0.45f;
    float box_conf_threshold = 0.25f;

    //rknn_init会拷贝，之后释放
    unsigned char         *model_data   = nullptr;
    rknn_context          ctx           = 0;
    rknn_input_output_num io_num        = {};
    rknn_tensor_attr      *input_attrs  = nullptr;
    rknn_tensor_attr      *output_attrs = nullptr;
    rknn_input            inputs[1]     = {};

    //模型输入尺寸，从attrs推导
    int channel =0, width = 0, height = 0;

    //letterbox还原参数
    BOX_RECT pads      = {};//四条边补的像素
    float    scale_lb  = 1.0f;//缩放比


    //输出量化参数，init的时候查用，复用
    std::vector<int32_t> out_zps;
    std::vector<float>   out_scales;

    //保护本实例（每个线程单独占用一个实例）
    std::mutex mtx;
};

#endif