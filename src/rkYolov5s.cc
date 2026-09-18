#include "rkYolov5s.hpp"
#include <stdio.h>

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

    //B2检查点
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