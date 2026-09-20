// =============================================================================
//  src/probe_video.cc  ——  视频解码探针（**诊断工具，不是学习内容**）
//
//  【要查的问题】
//      板子上 OpenCV 4.2 读任何视频都只解出 15~17% 的帧就卡死：
//          vbtest.mp4(260帧) -> 39     vt_ok_640x480(300) -> 50
//          手机1080p60(1217) -> 192
//      而系统 ffmpeg 能把同一个文件完整解出 260 帧。
//      所以问题在 OpenCV 这一侧。
//
//  【这个探针把 4 个可能的方向一次试完】
//      1. 颜色转换是不是元凶？  用 --norgb 关掉 BGR 转换，只拿原始 YUV
//      2. 后端是不是元凶？      用 --gst 换成 GStreamer 后端
//      3. Mat 复用是不是元凶？  用 --fresh 每帧新建 Mat
//      4. 是不是"偶发失败"？    用 --maxfail N 调 retry 阈值
//
//  【用法】
//      ./probe_video <视频文件> [--norgb] [--gst] [--fresh] [--maxfail N]
//
//  【怎么读结果】
//      "覆盖率 100%" -> 那个开关就是解药
//      "覆盖率还是 15%" -> 那个方向排除，换下一个
// =============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <opencv2/videoio.hpp>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr,
            "用法: %s <视频文件> [--norgb] [--gst] [--fresh] [--maxfail N]\n"
            "  --norgb       关掉 BGR 转换，只拿原始 YUV（测颜色转换是否元凶）\n"
            "  --gst         用 GStreamer 后端（默认 FFMPEG）\n"
            "  --fresh       每帧新建 cv::Mat（默认复用）\n"
            "  --maxfail N   连续失败几次才放弃（默认 5）\n", argv[0]);
        return -1;
    }

    const std::string path = argv[1];
    bool fresh   = false;
    bool norgb   = false;
    int  maxfail = 5;
    int  backend = cv::CAP_FFMPEG;

    for (int i = 2; i < argc; ++i)
    {
        const std::string a = argv[i];
        if      (a == "--fresh")   fresh = true;
        else if (a == "--norgb")   norgb = true;
        else if (a == "--gst")     backend = cv::CAP_GSTREAMER;
        else if (a == "--maxfail" && i + 1 < argc) maxfail = atoi(argv[++i]);
        else { fprintf(stderr, "未知选项: %s\n", a.c_str()); return -1; }
    }

    cv::VideoCapture cap;
    if (!cap.open(path, backend))
    {
        fprintf(stderr, "打不开（后端可能不支持）: %s\n", path.c_str());
        return -1;
    }

    if (norgb) cap.set(cv::CAP_PROP_CONVERT_RGB, 0);

    const double total = cap.get(cv::CAP_PROP_FRAME_COUNT);
    fprintf(stderr, "---- probe_video ----\n");
    fprintf(stderr, "文件      : %s\n", path.c_str());
    fprintf(stderr, "声称帧数  : %.0f   %.0fx%.0f @ %.1f fps\n", total,
            cap.get(cv::CAP_PROP_FRAME_WIDTH),
            cap.get(cv::CAP_PROP_FRAME_HEIGHT),
            cap.get(cv::CAP_PROP_FPS));
    fprintf(stderr, "后端      : %s\n", cap.getBackendName().c_str());
    fprintf(stderr, "选项      : %s%s%s   maxfail=%d\n",
            fresh ? "fresh " : "reuse ",
            norgb ? "norgb " : "bgr ",
            (backend == cv::CAP_GSTREAMER) ? "gst " : "ffmpeg ",
            maxfail);

    cv::Mat shared;
    int n = 0, consecutive = 0, max_consecutive = 0, total_fail = 0;
    double first_fail_wall = 0.0;
    const int64_t t0 = cv::getTickCount();
    const double  freq = cv::getTickFrequency();

    while (true)
    {
        bool ok;
        if (fresh) { cv::Mat tmp; ok = cap.read(tmp); if (ok && tmp.empty()) ok = false; }
        else       {              ok = cap.read(shared); if (ok && shared.empty()) ok = false; }

        if (ok) { ++n; consecutive = 0; continue; }

        ++consecutive; ++total_fail;
        if (total_fail == 1)
            first_fail_wall = (cv::getTickCount() - t0) * 1000.0 / freq;
        if (total_fail <= 3)
            fprintf(stderr, "  [失败 #%d] 在第 %d 帧之后  POS_FRAMES=%.0f  POS_MSEC=%.0f  墙钟=%.0f ms\n",
                    total_fail, n,
                    cap.get(cv::CAP_PROP_POS_FRAMES),
                    cap.get(cv::CAP_PROP_POS_MSEC),
                    (cv::getTickCount() - t0) * 1000.0 / freq);
        if (consecutive > max_consecutive) max_consecutive = consecutive;
        if (consecutive >= maxfail) break;
    }

    const double wall = (cv::getTickCount() - t0) * 1000.0 / freq;

    fprintf(stderr, "------------------------------------------------------\n");
    fprintf(stderr, "解出帧数    : %d\n", n);
    fprintf(stderr, "累计失败    : %d   最多连续失败: %d\n", total_fail, max_consecutive);
    fprintf(stderr, "首次失败时  : 墙钟 %.0f ms（每帧均 %.1f ms）\n",
            first_fail_wall, n ? wall / (n + total_fail) : 0.0);
    fprintf(stderr, "总墙钟      : %.0f ms\n", wall);
    if (total > 0)
    {
        fprintf(stderr, "覆盖率      : %.1f %%\n", 100.0 * n / total);
        fprintf(stderr, "%s\n", (n >= (int)total - 2)
                ? ">> ✅ 这个开关是解药！"
                : ">> ❌ 这个方向排除");
    }
    return 0;
}
