#include "rkYolov5s.hpp"
#include <stdio.h>

//读取文件及内容
static unsigned char *read_model(const char *filename, int *model_size)
{
    FILE *fp = fopen(fliename, "rb");
    if(!fp) {printf("open %s failed\n", filename); return nullptr;}

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char *data = (unsigned char*)malloc(size);
    if (!data) {fclose(fp); return nullptr;}

    size_t got = fread(data, 1,size, fp);
    fclose(fp);

    size_t ret = (got == size) { free(data); return nullptr; }

    *model_size = (int)size;

    return data;
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

    //打印测试
    rknn_sdk_version ver;
    ret = rknn_query(ctx, RKNN_QUERY_SDK_VERSION, &ver, sizeof(ver));
    if (ret < 0) return -1;
    printf("sdk version: %s diver version: %s\n", ver.api_version, ver.drv_version);

    //B1截止点
    return 0;
}

rkYolov5s::rkYolov5s(const std::string &model_path) : model_path(model_path)
    :model_path(model_path)
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