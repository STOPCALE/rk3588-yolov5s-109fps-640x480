// =============================================================================
//  e2_rga_probe.cc —— B10/E2：RGA 硬件预处理验证（2026-09-23）
// -----------------------------------------------------------------------------
//  回答的问题：
//    ① 用 RGA 做「640×480 放进 640×640（上下补灰）」每次多久？（= 当前扫描场景）
//    ② 缩放场景：1280×720 → 640×360 再放进 640×640，每次多久？
//    ③ CPU 对照：cv::resize + cv::copyMakeBorder（当前 letterbox 的实现）多慢？
//    ④ RGA 库与头文件版本是否匹配（imcheckHeader 自检）
//  原理说明（为什么视图可以直接当"子缓冲区"用）：
//    letterbox 的目标是「把 640×480 的原图写到 640×640 缓冲的第 80~559 行」。
//    因为 x 偏移为 0、宽度就是整行 640，第 80 行起的 480 行在内存里是连续的：
//        dst_view = dst_base + 80×640×3
//    所以只要把 dst_view 包成一个"640×480 的缓冲"交给 RGA，RGA 写的就是正确位置。
//    这样连 improcess 都不需要，imcopy/imresize 最简 API 即可完成 letterbox。
//  用法（板子上，先现场编译；用系统头文件 /usr/include/rga —— 与 librga 2.2.0 完全匹配）：
//    cd ~/myproj
//    g++ -O2 -std=c++14 -I /usr/include/rga tools/b10/e2_rga_probe.cc \
//        -o /tmp/e2_rga_probe -lrga $(pkg-config --cflags --libs opencv4)
//    taskset -c 4-7 /tmp/e2_rga_probe [帧jpg] [迭代次数]
//  产出：各方案 avg ms/次 + 两张结果图（/tmp/e2_A.jpg /tmp/e2_B.jpg）供目视检查
// =============================================================================
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <opencv2/opencv.hpp>
#include "im2d.h"

static double now_ms()
{
    using namespace std::chrono;
    return duration_cast<duration<double, std::milli>>(steady_clock::now().time_since_epoch()).count();
}

int main(int argc, char **argv)
{
    const char *jpg = (argc > 1) ? argv[1]
                                 : "/home/orangepi/videos/cam_20260923_125232/frame_000000.jpg";
    int iters = (argc > 2) ? atoi(argv[2]) : 1000;

    // ---- RGA 版本信息（库 / 头文件匹配检查）----
    printf("[rga] vendor : %s\n", querystring(RGA_VENDOR));
    printf("[rga] version: %s\n", querystring(RGA_VERSION));
    printf("[rga] imcheckHeader: %s\n", imStrError(imcheckHeader(RGA_CURRENT_API_HEADER_VERSION)));

    cv::Mat src = cv::imread(jpg, cv::IMREAD_COLOR);
    if (src.empty()) { printf("读图失败: %s\n", jpg); return -1; }
    printf("输入: %s  %dx%d\n", jpg, src.cols, src.rows);

    // ================= A. 真实场景：640×480 → 640×640（仅摆放，无缩放）=================
    {
        cv::Mat dstA(640, 640, CV_8UC3, cv::Scalar(114, 114, 114));   // 底色只填一次
        rga_buffer_t sA = wrapbuffer_virtualaddr(src.data, src.cols, src.rows, RK_FORMAT_BGR_888);
        unsigned char *dstA_top = dstA.data + 80 * 640 * 3;
        rga_buffer_t dA = wrapbuffer_virtualaddr(dstA_top, 640, 480, RK_FORMAT_BGR_888, 640, 480);

        IM_STATUS st = imcopy(sA, dA, 1);                              // 预热 + 正确性
        printf("[A] warmup imcopy: %s\n", imStrError(st));
        int ok = 0;   // 成功计数（修 bug：成功常量是 IM_STATUS_SUCCESS=1；NOERROR=2 不是成功判断）
        double t0 = now_ms();
        for (int i = 0; i < iters; i++)
        {
            st = imcopy(sA, dA, 1);
            if (!(st == IM_STATUS_SUCCESS || st == IM_STATUS_NOERROR))
            { printf("[A] 第 %d 次失败: %s\n", i, imStrError(st)); break; }
            ++ok;
        }
        double t1 = now_ms();
        printf("[A] RGA 摆放 (imcopy 640x480->640x640)    : %d/%d 次  avg %.4f ms\n",
               ok, iters, (t1 - t0) / (ok > 0 ? ok : 1));
        printf("[A] 正确性 |dst-src| 最大像素差 = %.0f（0 = 逐字节一致）\n",
               cv::norm(dstA(cv::Rect(0, 80, src.cols, src.rows)), src, cv::NORM_INF));
        cv::imwrite("/tmp/e2_A.jpg", dstA);
    }

    // ================= B. 缩放场景：1280×720 → 640×360 放进 640×640 =================
    {
        cv::Mat srcB;
        cv::resize(src, srcB, cv::Size(1280, 720));                    // 生成 720p 输入（测尺寸成本）
        cv::Mat dstB(640, 640, CV_8UC3, cv::Scalar(114, 114, 114));
        rga_buffer_t sB = wrapbuffer_virtualaddr(srcB.data, 1280, 720, RK_FORMAT_BGR_888);
        unsigned char *dstB_top = dstB.data + 140 * 640 * 3;
        rga_buffer_t dB = wrapbuffer_virtualaddr(dstB_top, 640, 360, RK_FORMAT_BGR_888, 640, 360);

        IM_STATUS st = imresize(sB, dB, 0.5, 0.5, 0, 1);               // 显式 0.5×（720p→360p），写入 640×360 视图
        printf("[B] warmup imresize: %s\n", imStrError(st));
        int ok = 0;
        double t0 = now_ms();
        for (int i = 0; i < iters; i++)
        {
            st = imresize(sB, dB, 0.5, 0.5, 0, 1);
            if (!(st == IM_STATUS_SUCCESS || st == IM_STATUS_NOERROR))
            { printf("[B] 第 %d 次失败: %s\n", i, imStrError(st)); break; }
            ++ok;
        }
        double t1 = now_ms();
        printf("[B] RGA 缩放 (720p->640x360->640x640)     : %d/%d 次  avg %.4f ms\n",
               ok, iters, (t1 - t0) / (ok > 0 ? ok : 1));
        cv::Mat refB;
        cv::resize(src, refB, cv::Size(640, 360));
        printf("[B] 正确性 |dst-参考| 最大像素差 = %.0f（与 cv::resize 的插值差异）\n",
               cv::norm(dstB(cv::Rect(0, 140, 640, 360)), refB, cv::NORM_INF));
        cv::imwrite("/tmp/e2_B.jpg", dstB);
    }

    // ================= A2. 句柄版 imcopy（importbuffer 一次，循环里复用）=================
    {
        cv::Mat dsth(640, 640, CV_8UC3, cv::Scalar(114, 114, 114));
        rga_buffer_handle_t hSrc = importbuffer_virtualaddr(src.data, src.cols, src.rows, RK_FORMAT_BGR_888);
        rga_buffer_handle_t hDst = importbuffer_virtualaddr(dsth.data + 80 * 640 * 3, 640, 480, RK_FORMAT_BGR_888);
        rga_buffer_t sh = wrapbuffer_handle(hSrc, src.cols, src.rows, RK_FORMAT_BGR_888);
        rga_buffer_t dh = wrapbuffer_handle(hDst, 640, 480, RK_FORMAT_BGR_888);
        IM_STATUS st = imcopy(sh, dh, 1);
        printf("[A2] warmup imcopy(handle): %s\n", imStrError(st));
        int ok = 0; double t0 = now_ms();
        for (int i = 0; i < iters; i++)
        {
            st = imcopy(sh, dh, 1);
            if (!(st == IM_STATUS_SUCCESS || st == IM_STATUS_NOERROR))
            { printf("[A2] 第 %d 次失败: %s\n", i, imStrError(st)); break; }
            ++ok;
        }
        double t1 = now_ms();
        printf("[A2] RGA 摆放（handle 版）                : %d/%d 次  avg %.4f ms\n",
               ok, iters, (t1 - t0) / (ok > 0 ? ok : 1));
        releasebuffer_handle(hSrc); releasebuffer_handle(hDst);
    }

    // ================= B2. 句柄版 imresize（720p → 640×360）=================
    {
        cv::Mat src720; cv::resize(src, src720, cv::Size(1280, 720));
        cv::Mat dsth(640, 640, CV_8UC3, cv::Scalar(114, 114, 114));
        rga_buffer_handle_t hSrc = importbuffer_virtualaddr(src720.data, 1280, 720, RK_FORMAT_BGR_888);
        rga_buffer_handle_t hDst = importbuffer_virtualaddr(dsth.data + 140 * 640 * 3, 640, 360, RK_FORMAT_BGR_888);
        rga_buffer_t sh = wrapbuffer_handle(hSrc, 1280, 720, RK_FORMAT_BGR_888);
        rga_buffer_t dh = wrapbuffer_handle(hDst, 640, 360, RK_FORMAT_BGR_888);
        IM_STATUS st = imresize(sh, dh, 0.5, 0.5, 0, 1);
        printf("[B2] warmup imresize(handle): %s\n", imStrError(st));
        int ok = 0; double t0 = now_ms();
        for (int i = 0; i < iters; i++)
        {
            st = imresize(sh, dh, 0.5, 0.5, 0, 1);
            if (!(st == IM_STATUS_SUCCESS || st == IM_STATUS_NOERROR))
            { printf("[B2] 第 %d 次失败: %s\n", i, imStrError(st)); break; }
            ++ok;
        }
        double t1 = now_ms();
        printf("[B2] RGA 缩放（handle 版）                : %d/%d 次  avg %.4f ms\n",
               ok, iters, (t1 - t0) / (ok > 0 ? ok : 1));
        releasebuffer_handle(hSrc); releasebuffer_handle(hDst);
    }

    // ================= C. CPU 对照（当前 letterbox 的实现）=================
    {
        cv::Mat scaled, outC;
        cv::Mat src720;
        cv::resize(src, src720, cv::Size(1280, 720));

        double t0 = now_ms();
        for (int i = 0; i < iters; i++)
        {
            cv::resize(src720, scaled, cv::Size(640, 360));
            cv::copyMakeBorder(scaled, outC, 140, 140, 0, 0, cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));
        }
        double t1 = now_ms();
        printf("[C] CPU 对照 (resize+border 720p->640x640) : %d 次  avg %.4f ms\n", iters, (t1 - t0) / iters);

        double t2 = now_ms();
        for (int i = 0; i < iters; i++)
        {
            cv::copyMakeBorder(src, outC, 80, 80, 0, 0, cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));
        }
        double t3 = now_ms();
        printf("[C2] CPU 对照 (仅 border 640x480->640x640): %d 次  avg %.4f ms\n", iters, (t3 - t2) / iters);

        // C3：每帧新建目标（最贴近当前 infer() 里 letterbox 的写法：dst 是局部新 Mat）
        double t4 = now_ms();
        for (int i = 0; i < iters; i++)
        {
            cv::Mat fresh;
            cv::copyMakeBorder(src, fresh, 80, 80, 0, 0, cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));
        }
        double t5 = now_ms();
        printf("[C3] CPU 对照 (每帧新建 dst 的 border)     : %d 次  avg %.4f ms\n", iters, (t5 - t4) / iters);
    }

    printf("完成。结果图: /tmp/e2_A.jpg（摆放）/ /tmp/e2_B.jpg（缩放）——拷回 PC 目视检查\n");
    return 0;
}
