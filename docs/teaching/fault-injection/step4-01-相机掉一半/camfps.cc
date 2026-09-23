// =============================================================================
//  step4 故障注入练习（坏版本）：USB 摄像头采集帧率统计。
//  构建/运行方法见同目录 README.md（需要摄像头，板子上跑）。
//  现象与工作表：docs/teaching/step4-视频循环计时/fault-injection.md
// =============================================================================
#include <opencv2/videoio.hpp>
#include <cstdio>
#include <chrono>

int main(int argc, char **argv)
{
    const char *dev = (argc > 1) ? argv[1] : "/dev/video0";

    cv::VideoCapture cap;
    if (!cap.open(dev, cv::CAP_V4L2))
    {
        printf("open %s fail (is a USB camera attached?)\n", dev);
        return 1;
    }

    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    cap.set(cv::CAP_PROP_FPS, 120);

    const int want_buf = 1;                          // 采集缓冲数
    cap.set(cv::CAP_PROP_BUFFERSIZE, want_buf);

    const int fc = (int)cap.get(cv::CAP_PROP_FOURCC);
    printf("[cam] %s: %dx%d %.1f fps '%c%c%c%c' buffer=%d\n",
           dev,
           (int)cap.get(cv::CAP_PROP_FRAME_WIDTH),
           (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT),
           cap.get(cv::CAP_PROP_FPS),
           (char)(fc & 0xFF), (char)((fc >> 8) & 0xFF),
           (char)((fc >> 16) & 0xFF), (char)((fc >> 24) & 0xFF),
           want_buf);

    const int N = 600;
    int ok = 0;
    double sum_ms = 0.0, mn = 0.0, mx = 0.0;

    auto t_start = std::chrono::steady_clock::now();
    cv::Mat frame;
    for (int i = 0; i < N; i++)
    {
        auto t0 = std::chrono::steady_clock::now();
        if (!cap.read(frame)) break;
        auto t1 = std::chrono::steady_clock::now();

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (ok == 0 || ms < mn) mn = ms;
        if (ok == 0 || ms > mx) mx = ms;
        sum_ms += ms;
        ok++;
    }
    auto t_end = std::chrono::steady_clock::now();
    double wall = std::chrono::duration<double>(t_end - t_start).count();

    printf("frames: %d/%d in %.2f s  ->  %.1f fps\n", ok, N, wall, ok / wall);
    printf("read: avg %.2f ms  min %.2f  max %.2f\n", sum_ms / ok, mn, mx);
    return 0;
}
