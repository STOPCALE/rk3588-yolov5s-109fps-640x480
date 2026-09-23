// =============================================================================
//  e3_dmabuf_probe.cc —— B10/E3：DMA-BUF 零拷贝喂 NPU 验证（2026-09-23）
// -----------------------------------------------------------------------------
//  回答的问题：
//    ① rknn_create_mem_from_fd + rknn_set_io_mem 在本板 runtime 上能不能用？
//    ② 用「消灭每帧 rknn_inputs_set 的 1.2MB 拷贝」能省多少时间？
//    ③ 零拷贝路径的输出与标准路径是否一致？
//
//  原理：
//    标准路径：每帧 rknn_inputs_set 把输入从用户内存**拷贝**进驱动；
//    零拷贝路径：把「外部 dma-buf」直接注册为模型输入内存（rknn_set_io_mem），
//               之后每帧 rknn_run 直接读那块内存 —— 无拷贝。
//    本探针用 /dev/dma_heap/system 申请 dma-buf，模拟「上游硬解 / RGA 产出的缓冲」。
//
//  用法（板子上）：
//    cd ~/myproj
//    g++ -O2 -std=c++14 -I include tools/b10/e3_dmabuf_probe.cc -o /tmp/e3_dmabuf_probe \
//        -L install/my_rknn_yolov5_demo_aarch64/lib -lrknnrt $(pkg-config --cflags --libs opencv4)
//    LD_LIBRARY_PATH=install/my_rknn_yolov5_demo_aarch64/lib taskset -c 4-7 /tmp/e3_dmabuf_probe \
//        install/my_rknn_yolov5_demo_aarch64/model/RK3588/best.rknn [帧jpg] [迭代数]
// =============================================================================
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <opencv2/opencv.hpp>
#include "rknn_api.h"

// ---- dma-heap ioctl（自包含定义，不依赖板端是否装了内核头文件）----
#define DMA_HEAP_IOC_MAGIC_LOCAL 'H'
struct dma_heap_allocation_data_local {
    uint64_t len;        // 申请长度（字节）
    uint32_t fd;         // [out] 分配得到的 dma-buf fd（内核回填）
    uint32_t fd_flags;   // O_RDWR | O_CLOEXEC
    uint64_t heap_flags; // 0
};
#define DMA_HEAP_IOCTL_ALLOC_LOCAL _IOWR(DMA_HEAP_IOC_MAGIC_LOCAL, 0x0, struct dma_heap_allocation_data_local)

static int dma_heap_alloc(uint64_t size, void **virt_out)
{
    int hfd = open("/dev/dma_heap/system", O_RDWR | O_CLOEXEC);
    if (hfd < 0) { printf("[dma] 打开 /dev/dma_heap/system 失败: %s\n", strerror(errno)); return -1; }
    struct dma_heap_allocation_data_local d;
    memset(&d, 0, sizeof d);
    d.len = size;
    d.fd_flags = O_RDWR | O_CLOEXEC;
    if (ioctl(hfd, DMA_HEAP_IOCTL_ALLOC_LOCAL, &d) < 0)
    { printf("[dma] ioctl ALLOC 失败: %s\n", strerror(errno)); close(hfd); return -1; }
    close(hfd);
    void *v = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, d.fd, 0);
    if (v == MAP_FAILED) { printf("[dma] mmap 失败: %s\n", strerror(errno)); close(d.fd); return -1; }
    *virt_out = v;
    return d.fd;
}

static uint32_t fnv1a(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

static double now_ms() { return cv::getTickCount() * 1000.0 / cv::getTickFrequency(); }

int main(int argc, char **argv)
{
    const char *model = (argc > 1) ? argv[1] : "install/my_rknn_yolov5_demo_aarch64/model/RK3588/best.rknn";
    const char *jpg   = (argc > 2) ? argv[2] : "/home/orangepi/videos/cam_20260923_125232/frame_000000.jpg";
    int iters = (argc > 3) ? atoi(argv[3]) : 200;

    // ---- 读模型 ----
    FILE *fp = fopen(model, "rb");
    if (!fp) { printf("打不开模型 %s\n", model); return -1; }
    fseek(fp, 0, SEEK_END); long msz = ftell(fp); fseek(fp, 0, SEEK_SET);
    std::vector<unsigned char> mdata(msz);
    if (fread(mdata.data(), 1, msz, fp) != (size_t)msz) { fclose(fp); printf("读模型失败\n"); return -1; }
    fclose(fp);

    rknn_context ctx = 0;
    int ret = rknn_init(&ctx, mdata.data(), (uint32_t)msz, 0, nullptr);
    if (ret < 0) { printf("rknn_init 失败 ret=%d\n", ret); return -1; }
    rknn_set_core_mask(ctx, RKNN_NPU_CORE_0);   // 单核即可（本实验测的是数据通路开销）

    rknn_input_output_num io_num;
    memset(&io_num, 0, sizeof io_num);
    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret < 0) { printf("query io_num 失败\n"); return -1; }
    printf("模型: %s（in=%u out=%u）\n", model, io_num.n_input, io_num.n_output);

    rknn_tensor_attr in_attr;
    memset(&in_attr, 0, sizeof in_attr);
    in_attr.index = 0;
    rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &in_attr, sizeof(in_attr));
    printf("input: dims=[%u,%u,%u,%u] fmt=%d type=%d pass=%u\n",
           in_attr.dims[0], in_attr.dims[1], in_attr.dims[2], in_attr.dims[3],
           in_attr.fmt, in_attr.type, in_attr.pass_through);

    // ---- 准备输入：帧 → letterbox 640×640（u8 BGR）→ int8 版本 ----
    cv::Mat fr = cv::imread(jpg, cv::IMREAD_COLOR);
    if (fr.empty()) { printf("读图失败 %s\n", jpg); return -1; }
    cv::Mat lb;
    if (fr.cols == 640 && fr.rows == 480)
        cv::copyMakeBorder(fr, lb, 80, 80, 0, 0, cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));
    else
        cv::resize(fr, lb, cv::Size(640, 640));
    const uint32_t INSZ = 640 * 640 * 3;
    std::vector<uint8_t> u8buf(INSZ);
    memcpy(u8buf.data(), lb.data, INSZ);
    std::vector<int8_t> i8buf(INSZ);
    for (uint32_t i = 0; i < INSZ; i++) i8buf[i] = (int8_t)((int)u8buf[i] - 128);

    // ---- 单独测 inputs_set 拷贝成本 ----
    {
        rknn_input in;
        memset(&in, 0, sizeof in);
        in.index = 0; in.buf = u8buf.data(); in.size = INSZ;
        in.pass_through = 0; in.type = RKNN_TENSOR_UINT8; in.fmt = RKNN_TENSOR_NHWC;
        int n = 500;
        double t0 = now_ms();
        for (int i = 0; i < n; i++) rknn_inputs_set(ctx, 1, &in);
        printf("[N] rknn_inputs_set 单独耗时: %.4f ms/次（每次拷贝 1.2MB 用户内存→驱动）\n",
               (now_ms() - t0) / n);
    }

    // ---- 标准路径 N：inputs_set + run + outputs ----
    uint32_t hashN = 0;
    double msN = 0;
    {
        rknn_input in;
        memset(&in, 0, sizeof in);
        in.index = 0; in.buf = u8buf.data(); in.size = INSZ;
        in.pass_through = 0; in.type = RKNN_TENSOR_UINT8; in.fmt = RKNN_TENSOR_NHWC;
        std::vector<rknn_output> outs(io_num.n_output);
        double t0 = now_ms();
        for (int i = 0; i < iters; i++)
        {
            rknn_inputs_set(ctx, 1, &in);
            rknn_run(ctx, nullptr);
            memset(outs.data(), 0, sizeof(rknn_output) * io_num.n_output);
            for (uint32_t k = 0; k < io_num.n_output; k++) { outs[k].index = k; outs[k].want_float = 0; outs[k].is_prealloc = 0; }
            rknn_outputs_get(ctx, io_num.n_output, outs.data(), nullptr);
            if (i == iters - 1) hashN = fnv1a((uint8_t *)outs[0].buf, outs[0].size);
            rknn_outputs_release(ctx, io_num.n_output, outs.data());
        }
        msN = (now_ms() - t0) / iters;
        printf("[N] 标准路径（inputs_set+run+outputs）: %.3f ms/帧 × %d 帧, hash=%08x\n", msN, iters, hashN);
    }

    // ---- 零拷贝路径 Z ----
    void *zvirt = nullptr;
    int zfd = dma_heap_alloc(INSZ, &zvirt);
    if (zfd < 0) { printf("[Z] 无法申请 dma-buf，跳过\n"); rknn_destroy(ctx); return 0; }
    memcpy(zvirt, i8buf.data(), INSZ);   // 预先把数据放进 dma-buf（之后不再拷贝）

    rknn_tensor_mem *zmem = rknn_create_mem_from_fd(ctx, zfd, zvirt, INSZ, 0);
    printf("[Z] rknn_create_mem_from_fd: %s (mem=%p virt=%p fd=%d size=%u)\n",
           zmem ? "OK" : "NULL", (void *)zmem, zmem ? zmem->virt_addr : nullptr,
           zmem ? zmem->fd : -1, zmem ? zmem->size : 0);

    rknn_tensor_attr zattr;
    memcpy(&zattr, &in_attr, sizeof zattr);
    zattr.index = 0;
    zattr.pass_through = 1;   // 原生格式直通（数据已按 int8 准备）
    ret = rknn_set_io_mem(ctx, zmem, &zattr);
    printf("[Z] set_io_mem(pass_through=1): ret=%d\n", ret);
    if (ret != 0)
    {
        memcpy(zvirt, u8buf.data(), INSZ);   // 变体：数据换回 u8
        memcpy(&zattr, &in_attr, sizeof zattr);
        zattr.index = 0; zattr.pass_through = 0; zattr.type = RKNN_TENSOR_UINT8;
        ret = rknn_set_io_mem(ctx, zmem, &zattr);
        printf("[Z] set_io_mem(pass_through=0, UINT8): ret=%d\n", ret);
    }

    if (ret == 0)
    {
        uint32_t hashZ = 0;
        std::vector<rknn_output> outs(io_num.n_output);
        double t0 = now_ms();
        for (int i = 0; i < iters; i++)
        {
            rknn_run(ctx, nullptr);   // 输入已在 dma-buf 里 —— 无 inputs_set
            memset(outs.data(), 0, sizeof(rknn_output) * io_num.n_output);
            for (uint32_t k = 0; k < io_num.n_output; k++) { outs[k].index = k; outs[k].want_float = 0; outs[k].is_prealloc = 0; }
            rknn_outputs_get(ctx, io_num.n_output, outs.data(), nullptr);
            if (i == iters - 1) hashZ = fnv1a((uint8_t *)outs[0].buf, outs[0].size);
            rknn_outputs_release(ctx, io_num.n_output, outs.data());
        }
        double msZ = (now_ms() - t0) / iters;
        printf("[Z] 零拷贝路径（run+outputs，无输入拷贝）: %.3f ms/帧 × %d 帧, hash=%08x\n", msZ, iters, hashZ);
        printf("[Z] 输出对比: %s\n", (hashZ == hashN) ? "一致 ✓（零拷贝链路正确）" : "⚠️ 不一致（待进一步排查）");
        printf("[Z] 收益: 标准 %.3f ms vs 零拷贝 %.3f ms → 省 %.3f ms/帧\n", msN, msZ, msN - msZ);
    }
    else
    {
        printf("[Z] set_io_mem 两种方式均失败 → 该 runtime 上此路径暂不可用\n");
    }

    if (zmem) rknn_destroy_mem(ctx, zmem);
    munmap(zvirt, INSZ);
    close(zfd);
    rknn_destroy(ctx);
    printf("完成\n");
    return 0;
}
