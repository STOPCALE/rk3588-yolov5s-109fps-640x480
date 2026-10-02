#include "rkYolov5s.hpp"
#include <stdio.h>
#include "coreNum.hpp"
#include <string.h>
#include <vector>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <stdlib.h>

//读取.rknn的内容
static unsigned char *read_model(const char * filename, int *model_size)
{
    //打开文件
    FILE *fp = fopen(filename, "rb");
    if (!fp) {printf("open %s faild\n", filename); return nullptr;}

    //读取文件大小
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    //开辟文件的存储区
    unsigned char *data = (unsigned char*)malloc(size);
    if (!data) {fclose(fp); return nullptr;}

    //将文件进行读取
    size_t got = fread(data, 1, size, fp);
    fclose(fp);

    //如果不对，释放区域
    if(got != (size_t)size) { free(data); return nullptr;}

    //什么意思？
    *model_size = (int)size;

    return data;
}

//letterbox：进行缩放填充等操作
static void letterbox(const cv::Mat &src, cv::Mat &dst, BOX_RECT &pads, float &scale,
                      const cv::Size &target,
                      const cv::Scalar &pad_color = cv::Scalar(114, 114, 114))
{
    //缩放比：从高和宽的比，取最小的那个
    //目标尺寸和原始尺寸的比值
    float sw = (float)target.width / (float)src.cols;
    float sh = (float)target.height / (float)src.rows;
    scale = std::min(sw, sh);

    //缩放后实际尺寸
    int new_w = (int)(src.cols * scale);
    int new_h = (int)(src.rows * scale);

    //如果缩放后尺寸和目标尺寸一样，就不进行填充
    cv::Mat scaled;;
    if (new_w == src.cols && new_h == src.rows)
    {
        scaled = src;
    }
    else
    {
        //缩放
        cv::resize(src, scaled, cv::Size(new_w, new_h));
    }

    //居中：分两半进行计算
    int dw      = target.width - new_w;
    int dh      = target.height - new_h;
    int left    = dw / 2;
    int top   = dh / 2;

    //赋予参数
    cv::copyMakeBorder(scaled, dst, top, dh - top, left, dw - left,
                        cv::BORDER_CONSTANT, pad_color);

    //记录还原参数
    pads.left   = left;
    pads.right  = dw - left;
    pads.top    = top;
    pads.bottom = dh - top;
}


//七步init
//1、读模型文件
//2、rknn初始化
//3、绑核
//4、打印sdk版本
//5、打印输入输出张量
//6、对输入张量和属性尺寸进行打印
//7、提取量化属性
int rkYolov5s::init(rknn_context *ctx_in, bool share_weight)
{
    //读模型文件
    int model_size = 0;
    //从头文件的类和初始化的时候读取文件的名字与大小
    model_data = read_model(model_path.c_str(), &model_size);
    if (!model_data) return -1;

    //初始化，如果是真，则进行dup，假就初始化，那这dup就是返回错误信息？
    int ret = share_weight ? rknn_dup_context(ctx_in, &ctx)
                            :rknn_init(&ctx, model_data, model_size, 0, nullptr);
    if (ret < 0) { printf("rknn_init error ret=%d\n", ret); return -1; }

    //这里进行选择，包括路径环境等，有无核之类的情况
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
            case 0:     core_mask = RKNN_NPU_CORE_0; break;
            case 1:     core_mask = RKNN_NPU_CORE_1; break;
            case 2:     core_mask = RKNN_NPU_CORE_2; break;
            default:    core_mask = RKNN_NPU_CORE_AUTO; break;
        }
    }

    //这路进行核绑定
    ret = rknn_set_core_mask(ctx, core_mask);
    if (ret < 0) { printf("rknn_set_core_mask error ret=%d\n", ret); return -1; }
    printf("bind: rotate_core=%d mask=%d\n", core, (int)core_mask);

    //打印测试,判断sdk版本
    rknn_sdk_version ver;
    ret = rknn_query(ctx, RKNN_QUERY_SDK_VERSION, &ver, sizeof(ver));
    if (ret < 0) return -1;
    printf("sdk version: %s diver version: %s\n", ver.api_version, ver.drv_version);

    //查询输入输出个数
    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret < 0) { printf("query in/out num error ret=%d\n", ret); return -1;}
    printf("model input num: %d, output num: %d\n", io_num.n_input, io_num.n_output);

    //查询输入张量属性
    input_attrs = (rknn_tensor_attr *) calloc(io_num.n_input, sizeof(rknn_tensor_attr));
    if (!input_attrs) { printf("calloc input_attrs failed\n"); return -1;}

    //不断读取属性以及输入输出
    for (uint32_t i = 0; i < io_num.n_input; i++)
    {
        input_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &input_attrs[i], sizeof(rknn_tensor_attr));
        if (ret < 0) { printf("query input attr[%d] error ret=%d\n", i, ret);}

        printf("input[%d]: name=%s, n_dims=%d, dims=[%d,%d,%d,%d], fmt=%d, type=%d\n",
            i, input_attrs[i].name, input_attrs[i].n_dims,
            input_attrs[i].dims[0],input_attrs[i].dims[1],
            input_attrs[i].dims[2],input_attrs[i].dims[3],
            input_attrs[i].fmt,input_attrs[i].type);
    }

    output_attrs = (rknn_tensor_attr *) calloc(io_num.n_output, sizeof(rknn_tensor_attr));
    if (!output_attrs) { printf("calloc output_attrs failed\n"); return -1;}

    //不断读取属性以及输入输出
    for (uint32_t i = 0; i < io_num.n_output; i++)
    {
        output_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &output_attrs[i], sizeof(rknn_tensor_attr));
        if (ret < 0) { printf("query input attr[%d] error ret=%d\n", i, ret); }

        printf("output[%d]: name=%s, n_dims=%d, dims=[%d,%d,%d,%d], fmt=%d, type=%d, zp=%d, scale=%f\n",
            i, output_attrs[i].name, output_attrs[i].n_dims,
            output_attrs[i].dims[0],output_attrs[i].dims[1],
            output_attrs[i].dims[2],output_attrs[i].dims[3],
            output_attrs[i].fmt,output_attrs[i].type,
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
        height    = input_attrs[0].dims[1];
        width     = input_attrs[0].dims[2];
        channel   = input_attrs[0].dims[3];
    }
    printf("model input: channe=%d, width=%d, height=%d\n", channel, width, height);

    //对输入进行处理
    inputs[0].index = 0;
    inputs[0].type  = RKNN_TENSOR_UINT8;
    inputs[0].fmt   = RKNN_TENSOR_NHWC;
    inputs[0].size  = (uint32_t)(width * height * channel);

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

//dup时，返回ctx指针
rknn_context *rkYolov5s::get_pctx()
{
    return &ctx;
}

//
FrameResult rkYolov5s::infer(cv::Mat &orig_img)
{
    //图片获取
    FrameResult result;
    result.image = orig_img;
    int ret = 0;

    //输入校验，判断图片是否为空或者通道是否为3
    if (orig_img.empty() || orig_img.channels() != 3)
    {
        printf("infer :bad input image (empty=%d, channels=%d)\n",
                (int)orig_img.empty(), orig_img.channels());
        return result;
    }
    double ms_pre = 0.0, ms_run = 0.0;

    //预处理：
    //这里输入图像数据,用来缩放图像
    int64_t t_pre = cv::getTickCount();
    cv::Mat resized;
    if (orig_img.cols == width && orig_img.rows == height
            && orig_img.channels() == channel && orig_img.isContinuous())
    {
        //原通道不缩放
        pads.left       = pads.right = pads.top = pads.bottom = 0;
        scale_lb        = 1.0f;
        inputs[0].buf   = orig_img.data;
    }
    else
    {
        //letterbox缩放
        letterbox(orig_img, resized, pads, scale_lb, cv::Size(width, height));
        inputs[0].buf = resized.data;
    }
    ms_pre = (cv::getTickCount() - t_pre) * 1000.0 / cv::getTickFrequency();

    //验证letterbox参数

    if (verbose)
    {
        printf("[lb] scale=%.6f new=%dx%d pads(l=%d r=%d t=%d b=%d)\n",
                scale_lb, width - pads.left - pads.right,
                height - pads.top - pads.bottom,
                pads.left, pads.right, pads.top, pads.bottom);
    }

    //初始四部
    //数据提交
    ret = rknn_inputs_set(ctx, io_num.n_input, inputs);
    if ( ret < 0 ) { printf("rknn_inputs_set error ret=%d\n", ret); return result; }

    //推理
    int64_t t_run = cv::getTickCount();
    ret = rknn_run(ctx, nullptr);
    if (ret < 0) { printf("rknn_run error ret=%d\n", ret); return result; }
    ms_run = (cv::getTickCount() - t_run) * 1000.0 / cv::getTickFrequency();

    //取出
    std::vector<rknn_output> outputs(io_num.n_output);
    memset(outputs.data(), 0, sizeof(rknn_output) * io_num.n_output);
    for (uint32_t i = 0; i < io_num.n_output; i++)
    {
        outputs[i].index        = i;
        outputs[i].want_float   = 0;
        outputs[i].is_prealloc  = 0;
    }

    ret = rknn_outputs_get(ctx, io_num.n_output, outputs.data(), nullptr);
    if (ret < 0)
    {
        printf("rknn_outputs_get error ret=%d\n", ret);
        rknn_outputs_release(ctx, io_num.n_output, outputs.data());
        return result;
    }

    //筛选加解码，主要筛选io_num
    if (io_num.n_output != VB_HEAD_NUM)
    {
        printf("infer：模型输出头数=%d, 当前解码只支持 %d 头\n",
                    io_num.n_output, VB_HEAD_NUM);
        rknn_outputs_release(ctx, io_num.n_output, outputs.data());
        return result;
    }

    vb_head_t heads[VB_HEAD_NUM];
    for (uint32_t i = 0; i < io_num.n_output; i++)
    {
        heads[i].data   = (const int8_t *)outputs[i].buf;
        heads[i].grid_h = (int)output_attrs[i].dims[2];
        heads[i].grid_w = (int)output_attrs[i].dims[3];
        heads[i].zp     = out_zps[i];
        heads[i].scale  = out_scales[i];
    }

    //坐标还原
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

    //可控显示
    if (verbose)
    {
        printf("[post] 候选=%d -> NMS后=%d decode=%.3f ms nms=%.3f ms 合计=%.3f\n",
                n_before, (int)vb.items.size(), ms_dec, ms_nms, ms_nms + ms_dec);
    }






    vb_to_original(vb.items, result.balls, pads, scale_lb);

    //打印结
    if (verbose)
    {
        for (int k = 0; k < (int)result.balls.size(); k++)
        {
            const vb_ball_t &b = result.balls[k];
            printf("[%d] cx=%.1f cy=%.1f r=%.1f prop=%.3f (原图坐标)\n",
                    k, b.cx, b.cy, b.radius, b.prop);
        }
    }

    //冒烟检测
    if (verbose)
    {
        for (uint32_t i = 0; i < io_num.n_output; i++)
        {
            int8_t *p   = (int8_t *)outputs[i].buf;
            int     n   = (int)outputs[i].size;
            int8_t  mn  = 127;
            int8_t  mx  = -128;
            uint32_t h  = 2166136261u;
            for (int k = 0; k < n; k++)
            {
                int8_t v =p[k];
                if (p[k] < mn) mn = v;
                if (p[k] > mx) mx = v;
                h = (h ^ (uint8_t)v) *  16777619u;
            }
            printf("out[%u] size-%d int8=[%d, max=%d] real[min=%.4f, max=%.4f] hash=%08x\n",
                    i, n, mn, mx,
                (mn - out_zps[i]) * out_scales[i],
                (mx - out_zps[i]) * out_scales[i],
                h);
        }
    }

    rknn_outputs_release(ctx, io_num.n_output, outputs.data());

    if (verbose)
    {
        printf("[time] preprocess=%.2f ms_run=%.2f ms\n", ms_pre, ms_run);
    }

    return result;

}

