// =============================================================================
//  step1 故障注入练习（坏版本 #2）：RKNN 最小链路 + 长跑 150 帧。
//  构建/运行方法见同目录 README.md（需要板子）。
//  现象与工作表：docs/teaching/step1-rknn最小链路/fault-injection.md
// =============================================================================
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

// 读 /proc/self/status 里的 VmRSS（单位 KB）
static long vmrss_kb()
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long kb = -1;
    while (fgets(line, sizeof line, f))
    {
        if (strncmp(line, "VmRSS:", 6) == 0) { sscanf(line + 6, "%ld", &kb); break; }
    }
    fclose(f);
    return kb;
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

    // 正确的 NCHW 读法（本故障与维度无关）
    int channel, height, width;
    if (in_attr.fmt == RKNN_TENSOR_NCHW)
    { channel = in_attr.dims[1]; height = in_attr.dims[2]; width = in_attr.dims[3]; }
    else
    { height = in_attr.dims[1]; width = in_attr.dims[2]; channel = in_attr.dims[3]; }
    printf("model input: channel=%d, width=%d, height=%d\n", channel, width, height);

    // 查询 3 个输出头，打印每帧输出总大小
    std::vector<rknn_tensor_attr> out_attrs(io_num.n_output);
    long out_bytes_per_frame = 0;
    for (uint32_t k = 0; k < io_num.n_output; k++)
    {
        out_attrs[k].index = k;
        ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &out_attrs[k], sizeof(rknn_tensor_attr));
        if (ret < 0) { printf("query output attr[%u] error=%d\n", k, ret); return 1; }
        // 粗略口径：非 batch 维乘积（含 stride 时应以 size_with_stride 为准）
        long bytes = (long)out_attrs[k].dims[1] * out_attrs[k].dims[2] * out_attrs[k].dims[3];
        out_bytes_per_frame += bytes;
        printf("output[%u]: dims=[%d,%d,%d,%d] bytes≈%ld\n", k,
               out_attrs[k].dims[0], out_attrs[k].dims[1],
               out_attrs[k].dims[2], out_attrs[k].dims[3], bytes);
    }
    printf("outputs total ≈ %.2f MB per frame\n", out_bytes_per_frame / 1048576.0);

    // 假输入
    std::vector<unsigned char> buf((size_t)width * height * channel, 114);
    rknn_input inputs[1] = {};
    inputs[0].index = 0;
    inputs[0].type  = RKNN_TENSOR_UINT8;
    inputs[0].fmt   = RKNN_TENSOR_NHWC;
    inputs[0].size  = (uint32_t)buf.size();
    inputs[0].buf   = buf.data();
    ret = rknn_inputs_set(ctx, 1, inputs);
    if (ret < 0) { printf("rknn_inputs_set error=%d\n", ret); return 1; }

    // 长跑 150 帧
    std::vector<rknn_output> outs(io_num.n_output);
    for (int frame = 1; frame <= 150; frame++)
    {
        ret = rknn_run(ctx, nullptr);
        if (ret < 0) { printf("rknn_run error=%d\n", ret); return 1; }

        for (uint32_t k = 0; k < io_num.n_output; k++)
        { outs[k].index = k; outs[k].want_float = 0; }

        ret = rknn_outputs_get(ctx, io_num.n_output, outs.data(), nullptr);
        if (ret < 0) { printf("outputs_get error=%d\n", ret); return 1; }

        // （本帧输出消费：看第一个字节）
        volatile unsigned char probe = ((unsigned char *)outs[0].buf)[0];
        (void)probe;

        if (frame % 30 == 0)
            printf("frame=%3d  VmRSS=%ld KB\n", frame, vmrss_kb());
    }

    rknn_destroy(ctx);
    free(model_data);
    return 0;
}
