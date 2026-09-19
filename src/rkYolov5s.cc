#include "rkYolov5s.hpp"
#include <stdio.h>
#include "coreNum.hpp"
#include <string.h>
#include <vector>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <stdlib.h>

//读取文件及内容
static unsigned char *read_model(const char *filename, int *model_size)
{
    FILE *fp = fopen(filename, "rb");
    if(!fp) {printf("open %s failed\n", filename); return nullptr;}

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char *data = (unsigned char*)malloc(size);
    if (!data) {fclose(fp); return nullptr;}

    size_t got = fread(data, 1,size, fp);
    fclose(fp);

    if (got != (size_t)size){ free(data); return nullptr; }

    *model_size = (int)size;

    return data;
}

//letterbox: 保持宽高比，缩放target尺寸，剩余则填充
static void letterbox(const cv::Mat &src, cv::Mat &dst, BOX_RECT &pads, float &scale,
                      const cv::Size &target,
                      const cv::Scalar &pad_color = cv::Scalar(114, 114, 114))
{
    //缩放比：又宽和高的比，取小值
    float sw = (float)target.width  / (float)src.cols;
    float sh = (float)target.height / (float)src.rows;
    scale = std::min(sw, sh);

    //缩放后的实际尺寸
    int new_w = (int)(src.cols * scale);
    int new_h = (int)(src.rows * scale);

    //缩放后如果和原图一样则跳过
    cv::Mat scaled;
    if (new_w == src.cols && new_h == src.rows)
    {
        scaled = src;
    }
    else
    {
        cv::resize(src, scaled, cv::Size(new_w, new_h));
    }

    //居中：分两半
    int dw   = target.width  - new_w;
    int dh   = target.height - new_h;
    int left = dw / 2;
    int top  = dh / 2;

    //参数顺序是（top, bottom, left, right）
    cv::copyMakeBorder(scaled, dst, top, dh - top, left, dw - left,
                        cv::BORDER_CONSTANT, pad_color);

    //记录还原参数
    pads.left   = left;
    pads.right  = dw - left;
    pads.top    = top;
    pads.bottom = dh - top;
}

int rkYolov5s::init(rknn_context *ctx_in, bool share_weight)
{
    //读模型文件
    int model_size = 0;
    model_data = read_model(model_path.c_str(), &model_size);
    if (!model_data) return -1;

    //初始化
    int ret = share_weight ? rknn_dup_context(ctx_in, &ctx)
                            :rknn_init(&ctx,model_data, model_size, 0, nullptr);
    if (ret < 0) { printf("rknn_init error ret = %d\n", ret); return -1;}

    //开始
    //绑核：按get_core_num()分配;
    //      亦可强制覆盖
    //      RKNN_CORE_MASK=1->只用核0，=2核1，=4核2，=7三核都用
    rknn_core_mask core_mask = RKNN_NPU_CORE_AUTO;
    int core = get_core_num();
    const char *env_mask = getenv("RKNN_CORE_MASK");
    if (env_mask != nullptr)
    {
        core_mask = (rknn_core_mask)atoi(env_mask);
    }
    else
    {
        switch (core)
        {
            case 0:  core_mask = RKNN_NPU_CORE_0; break;
            case 1:  core_mask = RKNN_NPU_CORE_1; break;
            case 2:  core_mask = RKNN_NPU_CORE_2; break;
            default: core_mask = RKNN_NPU_CORE_AUTO; break;
        }
    }
    ret = rknn_set_core_mask(ctx, core_mask);
    if (ret < 0) { printf("rknn_set_core_mask error ret=%d\n", ret); return -1; }
    printf("bind: rotate_core=%d mask=%d\n", core, (int)core_mask);

    //结束

    //打印测试
    rknn_sdk_version ver;
    ret = rknn_query(ctx, RKNN_QUERY_SDK_VERSION, &ver, sizeof(ver));
    if (ret < 0) return -1;
    printf("sdk version: %s diver version: %s\n", ver.api_version, ver.drv_version);

    // //B1截止点
    // return 0;

    //查询输入输出个数
    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret < 0) { printf("query in/out num error ret=%d\n", ret); return -1;}
    printf("model input num: %d, output num: %d\n", io_num.n_input, io_num.n_output);

    //查询输入张量属性
    input_attrs = (rknn_tensor_attr *)calloc(io_num.n_input, sizeof(rknn_tensor_attr));
    if (!input_attrs) {printf("calloc input_attrs failed\n"); return -1;}

    for (uint32_t i = 0; i < io_num.n_input; i++)
    {
        input_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &input_attrs[i], sizeof(rknn_tensor_attr));
        if (ret < 0 ) { printf("query input attr[%d] error ret=%d\n", i, ret); return -1; }

        printf("input[%d]: name=%s, n_dims=%d, dims=[%d,%d,%d,%d], fmt=%d, type=%d\n",
            i, input_attrs[i].name, input_attrs[i].n_dims,
            input_attrs[i].dims[0],input_attrs[i].dims[1],
            input_attrs[i].dims[2],input_attrs[i].dims[3],
            input_attrs[i].fmt,input_attrs[i].type);


    }

    // //B2检查点
    // return 0;

    //查输出属性+推导输入尺寸
    //查看输出属性张量
    output_attrs = (rknn_tensor_attr *)calloc(io_num.n_output, sizeof(rknn_tensor_attr));
    if(!output_attrs) { printf("calloc output_attrs failed\n"); return -1; }

    for (uint32_t i = 0; i < io_num.n_output; i++)
    {
        output_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &output_attrs[i], sizeof(rknn_tensor_attr));
        if (ret < 0) { printf("query output attr[%d] error ret=%d\n", i, ret); return -1; }

        printf("output[%d]: name=%s, n_dims=%d, dim=[%d,%d,%d,%d], fmt=%d, type=%d, zp=%d, scale=%f\n",
            i,output_attrs[i].name,output_attrs[i].n_dims,
            output_attrs[i].dims[0],output_attrs[i].dims[1],
            output_attrs[i].dims[2],output_attrs[i].dims[3],
            output_attrs[i].fmt, output_attrs[i].type,
            output_attrs[i].zp, output_attrs[i].scale);
    }

    //提取量化参数
    out_zps.resize(io_num.n_output);
    out_scales.resize(io_num.n_output);
    for (uint32_t i = 0; i < io_num.n_output; i++)
    {
        out_zps[i]      = output_attrs[i].zp;
        out_scales[i]   = output_attrs[i].scale;
    }

    //推导输入尺寸
    if (input_attrs[0].fmt == RKNN_TENSOR_NCHW)
    {
        channel = input_attrs[0].dims[1];
        height  = input_attrs[0].dims[2];
        width   = input_attrs[0].dims[3];
    }
    else
    {
        height = input_attrs[0].dims[1];
        width  = input_attrs[0].dims[2];
        channel   = input_attrs[0].dims[3];
    }
    printf("model input: channel=%d, width=%d, height=%d\n", channel, width, height);

    inputs[0].index = 0;
    inputs[0].type  = RKNN_TENSOR_UINT8;
    inputs[0].fmt   = RKNN_TENSOR_NHWC;
    inputs[0].size  = (uint32_t)(width * height *channel);

    //B3检查点
    return 0;
}

rkYolov5s::rkYolov5s(const std::string &model_path) : model_path(model_path)
{
    //构造函数
}

rkYolov5s::~rkYolov5s()
{
    //析构函数
    if (ctx)            rknn_destroy(ctx);
    if (model_data)     free(model_data);
    if (input_attrs)    free(input_attrs);
    if (output_attrs)   free(output_attrs);
}

//dup_context时，返回ctx指针
rknn_context *rkYolov5s::get_pctx()
{
    return &ctx;
}

//推理一帧：预处理->喂数据->RUN->取输出
FrameResult rkYolov5s::infer(cv::Mat &orig_img)
{
    FrameResult result;
    result.image = orig_img;
    int ret = 0;

    //输入校验
    if (orig_img.empty() || orig_img.channels() !=3)
    {
        printf("infer :bad input image (empty=%d, channels=%d)\n",
                (int)orig_img.empty(), orig_img.channels());
        return result;
    }
    double ms_pre = 0.0, ms_run = 0.0;

    //预处理：缩放到模型输入尺寸
    int64_t t_pre = cv::getTickCount();
    cv::Mat resized;
    if (orig_img.cols == width && orig_img.rows == height
        && orig_img.channels() == channel && orig_img.isContinuous())
    {
        //原图尺寸/通道匹配，不拷贝
        pads.left = pads.right = pads.top = pads.bottom = 0;
        scale_lb  = 1.0f;
        inputs[0].buf = orig_img.data;
    }
    else
    {
        //letterbox：保持缩放
        letterbox(orig_img, resized, pads, scale_lb, cv::Size(width, height));
        inputs[0].buf = resized.data;
    }
    ms_pre = (cv::getTickCount() - t_pre) * 1000.0 / cv::getTickFrequency();

    //B7验证letterbox参数
    printf("[lb] scale=%.6f new=%dx%d pads(l=%d r=%d t=%d b=%d)\n",
            scale_lb, width - pads.left - pads.right,
            height - pads.top - pads.bottom,
            pads.left, pads.right, pads.top, pads.bottom);

    //数据交给引擎
    ret = rknn_inputs_set(ctx, io_num.n_input, inputs);
    if (ret < 0) { printf("rknn_inputs_set error ret=%d\n", ret); return result; }

    //执行推理
    int64_t t_run = cv::getTickCount();
    ret = rknn_run(ctx, nullptr);
    if (ret < 0) { printf("rknn_run error ret=%d\n",ret); return result; }
    ms_run = (cv::getTickCount() - t_run) * 1000.0 / cv::getTickFrequency();

    //取出输出
    std::vector<rknn_output> outputs(io_num.n_output);
    memset(outputs.data(), 0,sizeof(rknn_output) * io_num.n_output);
    for (uint32_t i = 0; i < io_num.n_output; i++)
    {
        outputs[i].index        =i;
        outputs[i].want_float   =0;//此处为int8原始量化数据
        outputs[i].is_prealloc  =0;//此处为runtime分配buf，release时自动释放
    }
    ret = rknn_outputs_get(ctx, io_num.n_output, outputs.data(), nullptr);
    if (ret < 0) {  printf("rknn_outputs_get error ret=%d\n", ret); return result;}

    //B7-1预筛+解码
    vb_head_t heads[VB_HEAD_NUM];
    for (int i = 0; i < VB_HEAD_NUM; i++)
    {
        heads[i].data   = (const int8_t *)outputs[i].buf;
        heads[i].grid_h = (int)output_attrs[i].dims[2];     //NCHW
        heads[i].grid_w = (int)output_attrs[i].dims[3];
        heads[i].zp     = out_zps[i];
        heads[i].scale  = out_scales[i];
    }

    vb_result_t vb;

    //解码
    int64_t t_dec = cv::getTickCount();
    vb_decode(heads, width, height, box_conf_threshold, &vb);
    double ms_dec = (cv::getTickCount() - t_dec) * 1000.0 / cv::getTickFrequency();

    //NMS
    const int   n_before    = (int)vb.items.size();
    int64_t     t_nms       = cv::getTickCount();
    vb_nms(&vb, nms_threshold);
    double ms_nms = (cv::getTickCount() - t_nms) * 1000.0 / cv::getTickFrequency();

    printf("[post] 候选=%d -> NMS后=%d decode=%.3f ms nms=%.3f ms 合计=%.3f\n",
            n_before, (int)vb.items.size(), ms_dec, ms_nms, ms_nms + ms_dec);

    //打印幸存者（NMS 后通常只剩 1 个）—— 这就是最终的检测结果
    for (int k = 0; k < (int)vb.items.size(); k++)
    {
        const vb_box_t &b = vb.items[k];
        printf("  [%d] cx=%.1f cy=%.1f r=%.1f prop=%.3f  head=%d anchor=%d\n",
               k, b.cx, b.cy, b.radius, b.prop, b.head, b.anchor);
    }

    //冒烟检查，看数据如何
    for (uint32_t i = 0; i < io_num.n_output; i++)
    {
        int8_t *p  = (int8_t *)outputs[i].buf;
        int     n  = (int)outputs[i].size;
        int8_t  mn = 127;
        int8_t  mx = -128;
        uint32_t h = 2166136261u;
        for (int k = 0; k < n; k++)
        {
            int8_t v = p[k];
            if (p[k] < mn) mn = v;
            if (p[k] > mx) mx = v;
            h = (h ^ (uint8_t)v) * 16777619u;
        }
        printf("out[%u] size=%d int8[min=%d, max=%d] real[min=%.4f, max=%.4f] hash=%08x\n",
        i, n, mn, mx,
        (mn - out_zps[i]) * out_scales[i],
        (mx - out_zps[i]) * out_scales[i],
        h);
    }

    //释放输出
    rknn_outputs_release(ctx, io_num.n_output, outputs.data());

    printf("[time] preprocess=%.2f ms run=%.2f ms\n", ms_pre, ms_run);
    //B5检查点
    return result;
}