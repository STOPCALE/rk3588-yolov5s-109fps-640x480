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

    //逐帧调试输出开关（B8 加入）
    //  为什么需要它：infer() 里每帧要打十几行 printf，还包含一个遍历 15 万个输出元素的
    //  冒烟检查循环。跑单张图片时这是好用的“体测”，但跑视频/测 fps 时它就是纯负担——
    //  printf 到管道重定向每帧能吃掉好几毫秒，测出来的 fps 是“打印速度”，不是推理速度。
    //  放在实例成员上而不是定义成全局变量，是为了 B8-3 三线程时每个实例能各自控制。
    void set_verbose(bool on) { verbose = on; }

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

    //逐帧调试输出开关，默认开着，保持 B1~B7 的调试习惯不变
    bool verbose = true;
};

#endif