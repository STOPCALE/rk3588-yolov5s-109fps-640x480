// =============================================================================
//  step1 故障注入练习（坏版本 #1）：RKNN 最小链路。
//  构建/运行方法见同目录 README.md（需要板子）。
//  现象与工作表：docs/teaching/step1-rknn最小链路/fault-injection.md
// =============================================================================
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <vector>
#include "rknn_api.h"

static unsigned char *read_model(const char *path, int *size)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) { printf("open %s failed\n", path); return nullptr; }
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char *data = (unsigned char *)malloc(n);
    if (!data) { fclose(fp); return nullptr; }
    size_t got = fread(data, 1, n, fp);
    fclose(fp);
    if (got != (size_t)n) { free(data); return nullptr; }
    *size = (int)n;
    return data;
}

int main(int argc, char **argv)
{
    if (argc < 2) { printf("usage: %s model.rknn\n", argv[0]); return 1; }

    int model_size = 0;
    unsigned char *model_data = read_model(argv[1], &model_size);
    if (!model_data) return 1;

    rknn_context ctx = 0;
    int ret = rknn_init(&ctx, model_data, model_size, 0, nullptr);
    if (ret < 0) { printf("rknn_init error ret=%d\n", ret); return 1; }

    rknn_input_output_num io_num = {};
    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret < 0) { printf("query in/out num error=%d\n", ret); return 1; }
    printf("input num: %d, output num: %d\n", io_num.n_input, io_num.n_output);

    rknn_tensor_attr in_attr = {};
    in_attr.index = 0;
    ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &in_attr, sizeof(in_attr));
    if (ret < 0) { printf("query input attr error=%d\n", ret); return 1; }
    printf("input[0]: n_dims=%d dims=[%d,%d,%d,%d] fmt=%d type=%d\n",
           in_attr.n_dims, in_attr.dims[0], in_attr.dims[1], in_attr.dims[2],
           in_attr.dims[3], in_attr.fmt, in_attr.type);

    // ---- 推导输入尺寸 ----
    int channel = in_attr.dims[3];
    int height  = in_attr.dims[1];
    int width   = in_attr.dims[2];
    printf("model input: channel=%d, width=%d, height=%d\n", channel, width, height);

    if (channel != 3)
    {
        printf("bad input: channel=%d (expect 3)\n", channel);
        return 1;
    }

    // ---- 造一块假输入并跑一帧（修好维度问题后才会走到这里） ----
    const int n_px = width * height * channel;
    std::vector<unsigned char> buf(n_px, 114);   // 114 灰，仅做链路验证

    rknn_input inputs[1] = {};
    inputs[0].index = 0;
    inputs[0].type  = RKNN_TENSOR_UINT8;
    inputs[0].fmt   = RKNN_TENSOR_NHWC;
    inputs[0].size  = (uint32_t)n_px;
    inputs[0].buf   = buf.data();

    ret = rknn_inputs_set(ctx, 1, inputs);
    if (ret < 0) { printf("rknn_inputs_set error=%d\n", ret); return 1; }

    // 推理 50 次取平均
    double sum_ms = 0.0;
    for (int i = 0; i < 50; i++)
    {
        auto t0 = std::chrono::steady_clock::now();
        ret = rknn_run(ctx, nullptr);
        auto t1 = std::chrono::steady_clock::now();
        if (ret < 0) { printf("rknn_run error=%d\n", ret); return 1; }
        sum_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (i == 0)
        {
            std::vector<rknn_output> outs(io_num.n_output);
            for (uint32_t k = 0; k < io_num.n_output; k++)
            { outs[k].index = k; outs[k].want_float = 0; }
            ret = rknn_outputs_get(ctx, io_num.n_output, outs.data(), nullptr);
            if (ret < 0) { printf("outputs_get error=%d\n", ret); return 1; }
            for (uint32_t k = 0; k < io_num.n_output; k++)
                printf("output[%d]: size=%u, first bytes=[%02x %02x %02x]\n",
                       k, outs[k].size,
                       ((unsigned char *)outs[k].buf)[0],
                       ((unsigned char *)outs[k].buf)[1],
                       ((unsigned char *)outs[k].buf)[2]);
            rknn_outputs_release(ctx, io_num.n_output, outs.data());
        }
    }
    printf("rknn_run avg = %.2f ms\n", sum_ms / 50.0);

    rknn_destroy(ctx);
    free(model_data);
    return 0;
}
