// =============================================================================
//  docs/teaching/tools/probe_attrs.cc
//  「张量属性探针」：打印一个 .rknn 模型的全部输入/输出属性（不跑推理）。
//  用途：写教学材料/排查问题时拿"一手数据"。
//  构建与运行见同目录 README.md（在板子上跑）。
// =============================================================================
#include <cstdio>
#include <cstdlib>
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

static void print_attr(const char *tag, int i, const rknn_tensor_attr *a)
{
    printf("%s[%d] name=%s n_dims=%u dims=[", tag, i, a->name, a->n_dims);
    for (uint32_t d = 0; d < a->n_dims; d++)
        printf("%u%s", a->dims[d], d + 1 < a->n_dims ? "," : "");
    printf("] fmt=%d type=%d zp=%d scale=%g size=%u size_with_stride=%u\n",
           (int)a->fmt, (int)a->type, a->zp, a->scale, a->size, a->size_with_stride);
}

int main(int argc, char **argv)
{
    if (argc < 2) { printf("usage: %s model.rknn\n", argv[0]); return 1; }

    int model_size = 0;
    unsigned char *model_data = read_model(argv[1], &model_size);
    if (!model_data) return 1;
    printf("model file: %s (%d bytes)\n", argv[1], model_size);

    rknn_context ctx = 0;
    int ret = rknn_init(&ctx, model_data, model_size, 0, nullptr);
    if (ret < 0) { printf("rknn_init error ret=%d\n", ret); return 1; }

    rknn_sdk_version ver = {};
    if (rknn_query(ctx, RKNN_QUERY_SDK_VERSION, &ver, sizeof(ver)) == 0)
        printf("sdk version: %s  driver version: %s\n", ver.api_version, ver.drv_version);

    rknn_input_output_num io_num = {};
    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret < 0) { printf("query in/out num error=%d\n", ret); return 1; }
    printf("n_input=%u  n_output=%u\n", io_num.n_input, io_num.n_output);

    for (uint32_t i = 0; i < io_num.n_input; i++)
    {
        rknn_tensor_attr a = {};
        a.index = i;
        ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &a, sizeof(a));
        if (ret < 0) { printf("query input attr[%u] error=%d\n", i, ret); return 1; }
        print_attr("input", (int)i, &a);
    }

    for (uint32_t i = 0; i < io_num.n_output; i++)
    {
        rknn_tensor_attr a = {};
        a.index = i;
        ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &a, sizeof(a));
        if (ret < 0) { printf("query output attr[%u] error=%d\n", i, ret); return 1; }
        print_attr("output", (int)i, &a);
    }

    rknn_destroy(ctx);
    free(model_data);
    return 0;
}
