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
    //构造函数，规定函数必须以声明路径的方式出现
    explicit rkYolov5s(const std::string& model_path);
    ~rkYolov5s();

    //初始化7步
    int init(rknn_context *ctx_in = nullptr, bool share_weight = false);

    //返回ctx指针，尤其是在dup的时候
    rknn_context *get_pctx();

    //诊断开关
    void set_verbose(bool v) { verbose = v; }

    //推理函数
    FrameResult infer(cv::Mat &orig_img);

private:
    std::string model_path;
    //nms和conf的阈值
    float nms_threshold         = 0.45f;
    float box_conf_threshold    = 0.25f;

    //诊断开关
    bool verbose = false;

    //rknn_init需要的值
    unsigned char           *model_data     = nullptr;
    rknn_context            ctx             = 0;
    rknn_input_output_num   io_num          = {};
    rknn_tensor_attr        *input_attrs    = nullptr;
    rknn_tensor_attr        *output_attrs   = nullptr;
    rknn_input              inputs[1]       = {};

    //模型输入，从attrs推导
    int channel = 0, width = 0, height = 0;

    //letterbox还原参数
    BOX_RECT pads       = {};   //四条边补像素
    float    scale_lb   = 1.0f; //缩放比

    //输出量化参数，init用
    std::vector<int32_t> out_zps;
    std::vector<float>   out_scales;

    //保护一个实例
    std::mutex mtx;

};


#endif