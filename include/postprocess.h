#ifndef _RKNN_YOLOV5S_HPP_POSTPROCESS_H
#define _RKNN_YOLOV5S_HPP_POSTPROCESS_H

#include <stdint.h>
#include <vector>
// #include "opencv2/core/core.hpp"

//检测涉及的参数
#define OBJ_NAME_MAX_SIZE 16
#define OBJ_NUMB_MAX_SIZE 64
#define OBJ_CLASS_NUM     1
#define NMS_THRESH        0.45
#define BOX_THRESH        0.25
#define PROP_BOX_SIZE     (5 + OBJ_CLASS_NUM)

// 假设 BOX_RECT 已定义
typedef struct BOX_RECT {
    int left, right, top, bottom;
} BOX_RECT;

//后处理
//检测结果
typedef struct __detect_result_t
{
    char        name[OBJ_NAME_MAX_SIZE]; //类别名称
    BOX_RECT    box;                   //置信度
    float       prop;                 //边界框坐标
} detect_result_t;

//检测组
typedef struct __detect_result_group_t
{
    int     id;
    int     count;
    detect_result_t result[OBJ_NUMB_MAX_SIZE];
} detect_result_group_t;

//这是什么
int post_process(int8_t *input0, int8_t *input1, int8_t *input2, int model_in_h, int model_in_w,
                    float conf_threshold, float nms_threshold, BOX_RECT pads, float scale_w, float scale_h,
                    std::vector<int32_t> &qnt_zps, std::vector<float> &qnt_scales,
                    detect_result_group_t *group);

//这是什么
void deinitPostProcess();

#define VB_HEAD_NUM     3   //检测头数量
#define VB_ANCHOR_NUM   3   //每个检测头的anchor数
#define VB_PROP_SIZE    6   //每个anchor的输出长度=4(box)+1(obj)+1(cls)

//一个检测头
typedef struct _vb_head_t
{
    const int8_t *data;     //数据指针
    int          grid_w;    //网格宽度
    int          grid_h;    //网格高度
    int32_t      zp;        //量化零点
    float        scale;     //反量化
} vb_head_t;

//一个候选框
typedef struct _vb_box_t
{
    float cx, cy;   //中心点坐标
    float bh, bw;   //边界框宽高/宽
    float radius;   //半径
    float prop;     //置信度
    int   head;     //检测头索引
    int   anchor;   //anchor索引
} vb_box_t;

//
typedef struct _result_t
{
    std::vector<vb_box_t> items;    //通过的预筛选
} vb_result_t;

//预筛选+解码，返回筛选数量
int vb_decode(const vb_head_t head[VB_HEAD_NUM], int model_w, int model_g,
                float conf_thresh, vb_result_t *result);

//NMS抑制，返回存活数量
int vb_nms(vb_result_t *result, float iou_thresh);

//原图坐标系对排球检测结果
typedef struct _vb_ball_t
{
    float cx, cy;   //中心点坐标
    float radius;   //半径
    float prop;     //置信度
} vb_ball_t;

//把模型坐标系得检测结果还原到原图坐标系
void vb_to_original(const std::vector<vb_box_t> &src, std::vector<vb_ball_t> &dst,
                    const BOX_RECT &pads, float scale);

// 后处理函数声明
// std::vector<cv::Rect> postprocess(const std::vector<float>& outputs, float conf_threshold, float nms_threshold);

#endif // _RKNN_YOLOV5S_HPP_POSTPROCESS_H