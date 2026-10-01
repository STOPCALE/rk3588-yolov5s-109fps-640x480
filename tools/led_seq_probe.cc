// =============================================================================
//  tools/led_seq_probe.cc —— LED 亮度序列 + 帧时间戳（光参照延迟实验 · 主测量工具）
//  性质：**诊断工具**（不进入主链路；可复用于任何“亮度随时间变化”的验证）
//
//  【干什么】连续采集相机画面，逐帧记录：
//      ① ROI 区域（默认：受控绿灯窗口）的 G 通道 均值/最大值
//      ② 该帧的读取时刻（cv::getTickCount，**与主程序采集戳同轴**）
//    输出 CSV：n,t_ms,g_mean,g_max
//
//  【为什么】LED 由板子控制（发光时刻记录于 led_flash.sh 的墙钟 CSV，µs 级精度）；
//    相机拍 LED 亮/灭 → “LED 发光 → 软件拿到帧”的延迟可直接对账。
//    跳变帧（曝光窗被 LED 跳变切开）亮度是中间值 → 定位精度 ≈ 曝光时长（ms 级）。
//
//  【用法】板子上：
//      g++ -O2 led_seq_probe.cc -o /tmp/led_seq_probe $(pkg-config --cflags --libs opencv4)
//      taskset -c 4-7 /tmp/led_seq_probe /dev/video0 --sec 20 --roi 255,57,285,87 --csv /tmp/seq.csv
//  参数：--sec N | --roi x0,y0,x1,y1 | --fps N | --buf N（驱动缓冲，默认 2）| --csv path
// =============================================================================
#include <opencv2/opencv.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

static double now_ms()
{
    return cv::getTickCount() * 1000.0 / cv::getTickFrequency();
}

int main(int argc, char **argv)
{
    const char *dev = "/dev/video0";
    int sec = 20, fps = 120, buf = 2;
    int rx0 = 255, ry0 = 57, rx1 = 285, ry1 = 87;
    const char *csv = "/tmp/led_seq.csv";

    for (int i = 1; i < argc; i++)
    {
        if      (!strcmp(argv[i], "--sec") && i + 1 < argc) sec = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fps") && i + 1 < argc) fps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--buf") && i + 1 < argc) buf = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--roi") && i + 1 < argc)
            sscanf(argv[++i], "%d,%d,%d,%d", &rx0, &ry0, &rx1, &ry1);
        else if (!strcmp(argv[i], "--csv") && i + 1 < argc) csv = argv[++i];
        else if (argv[i][0] != '-')                          dev = argv[i];
    }

    cv::VideoCapture cap;
    if (!cap.open(dev, cv::CAP_V4L2)) { printf("open fail: %s\n", dev); return -1; }
    if (buf > 0) cap.set(cv::CAP_PROP_BUFFERSIZE, buf);
    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    cap.set(cv::CAP_PROP_FPS, fps);

    cv::Mat img;
    for (int i = 0; i < 8; i++) cap.read(img);   // 预热

    FILE *fo = fopen(csv, "w");
    if (!fo) { printf("csv open fail: %s\n", csv); return -1; }
    fprintf(fo, "n,t_ms,g_mean,g_max\n");

    printf("roi=(%d,%d)-(%d,%d) sec=%d buf=%d\n", rx0, ry0, rx1, ry1, sec, buf);

    const double t_end = now_ms() + sec * 1000.0;
    int n = 0;
    while (true)
    {
        if (!cap.read(img)) break;
        const double t = now_ms();               // read 返回即刻（与主程序采集戳同口径）
        if (t >= t_end) break;

        const int x0 = std::max(0, std::min(rx0, img.cols - 1));
        const int y0 = std::max(0, std::min(ry0, img.rows - 1));
        const int x1 = std::max(x0 + 1, std::min(rx1, img.cols - 1));
        const int y1 = std::max(y0 + 1, std::min(ry1, img.rows - 1));

        cv::Mat g;
        cv::extractChannel(img, g, 1);
        cv::Mat sub = g(cv::Rect(x0, y0, x1 - x0, y1 - y0));
        const double gmean = cv::mean(sub)[0];
        double gmin = 0, gmax = 0;
        cv::minMaxLoc(sub, &gmin, &gmax);

        fprintf(fo, "%d,%.3f,%.2f,%.0f\n", n, t, gmean, gmax);
        n++;
    }
    fclose(fo);
    printf("rows=%d -> %s\n", n, csv);
    return 0;
}
