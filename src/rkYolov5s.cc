#include "rkYolov5s.h"

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
    printf("sdk version: %s diver version: %s\n", ver.api_version, ver.drv_versinon);

    //B1截止点
    return 0;
}

