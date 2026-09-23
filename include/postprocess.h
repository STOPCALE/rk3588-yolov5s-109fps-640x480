#ifndef _RKNN_YOLOV5_DEMO_POSTPROCESS_H_
#define _RKNN_YOLOV5_DEMO_POSTPROCESS_H_

#include <stdint.h>
#include <vector>

#define OBJ_NAME_MAX_SIZE 16
#define OBJ_NUMB_MAX_SIZE 64
#define OBJ_CLASS_NUM 1
#define NMS_THRESH 0.45
#define BOX_THRESH 0.6
#define PROP_BOX_SIZE (5 + OBJ_CLASS_NUM)

typedef struct _BOX_RECT
{
    int left;
    int right;
    int top;
    int bottom;
} BOX_RECT;

//-----------------------------------------------------------------------------
// 【教学化改造注】以下为原工程旧接口（post_process / deinitPostProcess / detect_result_t）：
//   当前工程已改用下方的 vb_* 新实现（vb_decode / vb_nms / vb_to_original）。
//   旧接口【未实现、未调用】，仅作对照参考 —— 属"待重写范围"。
//-----------------------------------------------------------------------------
typedef struct __detect_result_t
{
    char name[OBJ_NAME_MAX_SIZE];
    BOX_RECT box;
    float prop;
} detect_result_t;

typedef struct _detect_result_group_t
{
    int id;
    int count;
    detect_result_t results[OBJ_NUMB_MAX_SIZE];
} detect_result_group_t;

int post_process(int8_t *input0, int8_t *input1, int8_t *input2, int model_in_h, int model_in_w,
                 float conf_threshold, float nms_threshold, BOX_RECT pads, float scale_w, float scale_h,
                 std::vector<int32_t> &qnt_zps, std::vector<float> &qnt_scales,
                 detect_result_group_t *group);

void deinitPostProcess();

#define VB_HEAD_NUM     3 //检测头数量
#define VB_ANCHOR_NUM   3 //每个头的anchor数
#define VB_PROP_SIZE    6 //每个anchor的输出长度=4(box)+1(obj)+1(cls)

//一个检测头
typedef struct _vb_head_t
{
    const int8_t *data;     //该头的输出数据
    int           grid_w;   //网络宽度
    int           grid_h;   //网格高
    int32_t       zp;       //放量化
    float         scale;   //反量化
} vb_head_t;

//一个候选框
typedef struct _vb_box_t
{
    float cx, cy;   //球心
    float bw, bh;   //外接框的宽/高
    float radius;   //半径
    float prop;     //置信度
    int   head;     //来自哪个头
    int   anchor;   //来自哪个anchor
} vb_box_t;

typedef struct _result_t
{
    std::vector<vb_box_t> items;    //所有通过预筛的候选
} vb_result_t;

//预筛+解码，返回候选数量
int vb_decode(const vb_head_t heads[VB_HEAD_NUM], int model_w, int model_h,
                float conf_thresh, vb_result_t *result);

//NMS抑制（就地修改result->items）返回存活数量
int vb_nms(vb_result_t * result, float iou_thresh);

//原图坐标系下对排球检测结果
typedef struct _vb_ball_t
{
    /* data */
    float cx,cy;    //球心（原图坐标）
    float radius;   //半径（原像素）
    float prop;     //置信度
} vb_ball_t;

//把模型坐标系得检测结果还原到原图坐标系
//   x_orig = (x_model - pad.left) / scale
//   y_orig = (y_model - pad.top ) / scale
//   r_orig =         r_model      / scale     ← 半径是"长度"，【不減 pad】！
void vb_to_original(const std::vector<vb_box_t> &src, std::vector<vb_ball_t> &dst,
                        const BOX_RECT &pads, float scale);

#endif //_RKNN_YOLOV5_DEMO_POSTPROCESS_H_
