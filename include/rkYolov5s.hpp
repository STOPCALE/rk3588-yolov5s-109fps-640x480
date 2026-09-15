#ifndef RKYOLOV5S_HPP
#define RKYOLOV5S_HPP

#include <string>
#include <mutex>
#include "rknn_api.h"
#include "opencv2/core/core.hpp"
#include "FrameResult.hpp"

class rkYolov5s
{
public:
    rkYolov5s(const std::string &model_path);
    ~rkYolov5s();

    int init(rknn_context *ctx_in = nullptr, bool share_weight = false);
    rknn_context *get_pctx();

    // 改这里：原来是 cv::Mat infer(cv::Mat &orig_img);
    FrameResult infer(cv::Mat &orig_img);

    static void set_uart_fd(int fd) { uart_fd = fd; }

private:
    std::string model_path;

    float nms_threshold;
    float box_conf_threshold;

    unsigned char *model_data;
    rknn_context ctx;
    rknn_input_output_num io_num;
    rknn_tensor_attr *input_attrs;
    rknn_tensor_attr *output_attrs;
    rknn_input inputs[1];
    int ret;

    int channel;
    int width;
    int height;
    int img_width;
    int img_height;

    std::mutex mtx;

    static int uart_fd;
    static std::mutex uart_mutex;
};

#endif