// =============================================================================
//  e1_appsink_probe.cc —— B10/E1 第二段：mppjpegdec → OpenCV 交付路径验证
// -----------------------------------------------------------------------------
//  回答的核心问题：
//    「OpenCV 能不能直接消费 mppjpegdec 解出来的帧？每帧读帧开销多少？」
//    这决定采集管线能否从「OpenCV 软解（read ≈ 9.1 ms/帧）」换成「硬解 + 直通」。
//
//  依次跑这些模式（自动开关相机）：
//    M0 基线      ：cv::VideoCapture 直开 /dev/video0 (CAP_V4L2, MJPG, buffer=2)
//                   —— 当前主程序走的路（同一天对照基准）
//    M1 硬解直通  ：v4l2src ! mppjpegdec ! video/x-raw,format=BGR ! appsink
//    M1b 直通备选 ：v4l2src ! mppjpegdec ! appsink（让 OpenCV 自己协商 caps，仅 M1 失败时跑）
//    M2 硬解+软转 ：v4l2src ! mppjpegdec ! videoconvert ! BGR ! appsink（对照）
//    M3 文件正确性：filesrc 单帧 ! mppjpegdec ! BGR ! appsink → 与 cv::imread 逐像素对比
//
//  用法（板子上）：
//    cd ~/myproj
//    g++ -O2 -std=c++14 tools/b10/e1_appsink_probe.cc -o /tmp/e1_appsink_probe \
//        $(pkg-config --cflags --libs opencv4)
//    taskset -c 4-7 /tmp/e1_appsink_probe [相机帧数=600]
// =============================================================================
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <opencv2/opencv.hpp>

static int g_frames = 600;

static void report(const char *tag, const std::vector<double> &ts, double wall_ms, int got)
{
    if (ts.empty()) { printf("[%s] 未读到帧！\n", tag); return; }
    double sum = 0, mn = 1e9, mx = 0;
    for (double t : ts) { sum += t; if (t < mn) mn = t; if (t > mx) mx = t; }
    printf("[%s] %d 帧 | read avg %.2f  min %.2f  max %.2f ms | 墙钟 %.2f s → %.1f fps\n",
           tag, got, sum / ts.size(), mn, mx, wall_ms / 1000.0, got * 1000.0 / wall_ms);
}

// 通用：用 gst pipeline 打开相机、读 g_frames 帧、统计 read 耗时
static int run_pipeline(const char *tag, const char *pl, const char *snap_path)
{
    cv::VideoCapture cap;
    if (!cap.open(pl, cv::CAP_GSTREAMER)) { printf("[%s] pipeline 打开失败\n", tag); return 0; }
    const double tick = cv::getTickFrequency();
    cv::Mat f;
    for (int i = 0; i < 10; i++) cap.read(f);              // 预热
    std::vector<double> ts; int got = 0;
    double t0 = cv::getTickCount();
    while (got < g_frames)
    {
        int64_t a = cv::getTickCount();
        if (!cap.read(f)) break;
        ts.push_back((cv::getTickCount() - a) * 1000.0 / tick);
        ++got;
    }
    double wall = (cv::getTickCount() - t0) * 1000.0 / tick;
    report(tag, ts, wall, got);
    if (got > 0 && snap_path) cv::imwrite(snap_path, f);
    cap.release();
    return got;
}

int main(int argc, char **argv)
{
    if (argc > 1) g_frames = atoi(argv[1]);
    const double tick = cv::getTickFrequency();

    // ---------- M0 基线：OpenCV 直接开 V4L2（当前主程序路径）----------
    {
        cv::VideoCapture cap;
        if (cap.open("/dev/video0", cv::CAP_V4L2))
        {
            cap.set(cv::CAP_PROP_BUFFERSIZE, 2);
            cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
            cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
            cap.set(cv::CAP_PROP_FPS, 120);
            cv::Mat f;
            for (int i = 0; i < 10; i++) cap.read(f);
            std::vector<double> ts; int got = 0;
            double t0 = cv::getTickCount();
            while (got < g_frames)
            {
                int64_t a = cv::getTickCount();
                if (!cap.read(f)) break;
                ts.push_back((cv::getTickCount() - a) * 1000.0 / tick);
                ++got;
            }
            double wall = (cv::getTickCount() - t0) * 1000.0 / tick;
            report("M0 基线 V4L2软解", ts, wall, got);
            if (got > 0) cv::imwrite("/tmp/b10_M0.jpg", f);
            cap.release();
        }
        else printf("[M0] 打不开 /dev/video0\n");
    }

    // ---------- M1 / M1b：硬解直通 ----------
    const char *CAM  = "v4l2src device=/dev/video0 num-buffers=1300 ! image/jpeg,width=640,height=480,framerate=120/1 ! ";
    const char *SINK = "appsink sync=false max-buffers=2 drop=true";
    {
        char pl[1024];
        snprintf(pl, sizeof pl, "%smppjpegdec ! video/x-raw,format=BGR ! %s", CAM, SINK);
        int got = run_pipeline("M1 硬解直通 mpp!BGR!appsink", pl, "/tmp/b10_M1.jpg");
        if (got == 0)
        {
            snprintf(pl, sizeof pl, "%smppjpegdec ! %s", CAM, SINK);
            int got1b = run_pipeline("M1b 硬解直通(自动协商)", pl, "/tmp/b10_M1b.jpg");
            if (got1b == 0)
            {
                // 2026-09-23 已试（记录见 B10 验证报告）：dma-feature=false 直通（M1c）仍 0 帧
                // （gst_buffer_resize_range 断言失败）；M1d（format=16 变体）会直接 core dump。
                // 结论：本栈（OpenCV 4.2 + gst-rockchip 1.14.4）下 mppjpegdec 直通 appsink 不可行。
                printf("[M1c/M1d] 跳过（已断定为不可行，避免崩溃；详情见报告）\n");
            }
        }
    }

    // ---------- M2：硬解 + 软转（对照）----------
    {
        char pl[1024];
        snprintf(pl, sizeof pl, "%smppjpegdec ! videoconvert ! video/x-raw,format=BGR ! %s", CAM, SINK);
        run_pipeline("M2 硬解+videoconvert", pl, nullptr);
    }

    // ---------- M4：原始 MJPEG 直读（不解码）——分离「V4L2/拷贝」与「解码」的成本 ----------
    {
        cv::VideoCapture cap;
        if (cap.open("/dev/video0", cv::CAP_V4L2))
        {
            cap.set(cv::CAP_PROP_BUFFERSIZE, 2);
            cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
            cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
            cap.set(cv::CAP_PROP_FPS, 120);
            cap.set(cv::CAP_PROP_CONVERT_RGB, 0);          // 拿原始 JPEG 字节（不软解）
            cv::Mat f;
            for (int i = 0; i < 10; i++) cap.read(f);
            std::vector<double> ts; int got = 0;
            double t0 = cv::getTickCount();
            while (got < g_frames)
            {
                int64_t a = cv::getTickCount();
                if (!cap.read(f)) break;
                ts.push_back((cv::getTickCount() - a) * 1000.0 / tick);
                ++got;
            }
            double wall = (cv::getTickCount() - t0) * 1000.0 / tick;
            report("M4 原始MJPEG直读(CONVERT_RGB=0)", ts, wall, got);
            if (!f.empty() && f.total() > 1)
                printf("[M4] 首两字节=%02x %02x（ff d8 = 原始 JPEG 确认）\n",
                       (unsigned char)f.data[0], (unsigned char)f.data[1]);
            cap.release();
        }
        else printf("[M4] 打不开 /dev/video0\n");
    }

    // ---------- M3：文件正确性（硬解 vs OpenCV 软解）----------
    {
        const char *jpg = "/home/orangepi/videos/cam_20260923_125232/frame_000000.jpg";
        const char *mjpeg = "/tmp/b10_all10.mjpeg";            // 单文件小流 gst 兼容有问题，改用拼接大流
        char pl[1024];
        snprintf(pl, sizeof pl,
                 "filesrc location=%s ! image/jpeg ! jpegparse ! mppjpegdec ! video/x-raw,format=BGR ! %s",
                 mjpeg, SINK);
        cv::VideoCapture cap;
        if (cap.open(pl, cv::CAP_GSTREAMER))
        {
            cv::Mat hw;
            bool ok = cap.read(hw);
            cv::Mat sw = cv::imread(jpg, cv::IMREAD_COLOR);
            if (ok && !hw.empty() && !sw.empty() && hw.size() == sw.size())
            {
                double inf = cv::norm(hw, sw, cv::NORM_INF);
                printf("[M3] 硬解 vs OpenCV 软解：尺寸一致 %dx%d，最大像素差 = %.0f（<20 属解码器实现差异）\n",
                       hw.cols, hw.rows, inf);
                cv::imwrite("/tmp/b10_M3_hw.jpg", hw);
                cv::imwrite("/tmp/b10_M3_sw.jpg", sw);
            }
            else printf("[M3] 读帧失败或尺寸不一致（ok=%d, hw=%dx%d, sw=%dx%d）\n",
                        (int)ok, hw.empty() ? 0 : hw.cols, hw.empty() ? 0 : hw.rows,
                        sw.empty() ? 0 : sw.cols, sw.empty() ? 0 : sw.rows);
            cap.release();
        }
        else printf("[M3] pipeline 打开失败\n");
    }

    printf("完成。样张: /tmp/b10_M0.jpg /tmp/b10_M1.jpg /tmp/b10_M3_hw.jpg\n");
    return 0;
}
