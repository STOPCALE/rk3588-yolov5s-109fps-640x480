// =============================================================================
//  tools/screen_clock.cc —— 全屏毫秒时钟（“绝对延迟”光参照实验的现实时间标签）
//  性质：**诊断工具**（为延迟测量服务；不进入主链路）
//
//  【干什么】在 HDMI 显示器上全屏显示板子自己的墙钟（CLOCK_REALTIME，与 date 同源），
//            毫秒精度。相机对着它拍 → 画面里每一帧都带一个“现实时刻读数”。
//            对账：结果可用时刻(板子墙钟) − 帧图里读到的时刻 = “现实→结果”延迟。
//
//  【为什么用板子的钟】屏幕读数与板子日志天然同一条时间轴（这是相减的前提）。
//            屏幕自身的出屏延迟/刷新粒度计入“大致校对”的容差（±10ms 级），
//            该实验定位是粗测，不追求两位小数。
//
//  【用法】板子上：
//      g++ -O2 screen_clock.cc -o /tmp/screen_clock $(pkg-config --cflags --libs opencv4)
//      DISPLAY=:0 setsid /tmp/screen_clock >/tmp/screen_clock.log 2>&1 < /dev/null &
//      # 停止：pkill screen_clock
//      可带参数指定尺寸：/tmp/screen_clock 1680 1050（默认会自动读窗口全屏）
// =============================================================================
#include <opencv2/opencv.hpp>
#include <ctime>
#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv)
{
    int w = (argc > 1) ? atoi(argv[1]) : 1680;
    int h = (argc > 2) ? atoi(argv[2]) : 1050;

    cv::namedWindow("clock", cv::WINDOW_NORMAL);
    cv::imshow("clock", cv::Mat::zeros(h, w, CV_8UC3));   // 先映射一次，让 WM 接管窗口
    cv::waitKey(200);
    cv::moveWindow("clock", 0, 0);
    cv::resizeWindow("clock", w, h);
    cv::waitKey(100);
    cv::setWindowProperty("clock", cv::WND_PROP_FULLSCREEN, cv::WINDOW_FULLSCREEN);
    cv::waitKey(200);

    cv::Mat img(h, w, CV_8UC3);
    for (;;)
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        struct tm tmv;
        localtime_r(&ts.tv_sec, &tmv);

        char big[64];
        snprintf(big, sizeof big, "%02d:%02d:%02d.%03ld",
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ts.tv_nsec / 1000000L);

        img = cv::Scalar(0, 0, 0);

        double scale = 1.0;
        int    thick = 6;
        int    base  = 0;
        cv::Size sz;
        do
        {
            scale += 0.5;
            sz = cv::getTextSize(big, cv::FONT_HERSHEY_SIMPLEX, scale, thick, &base);
        } while (sz.width < (int)(w * 0.92) && scale < 40.0);
        scale -= 0.5;
        sz = cv::getTextSize(big, cv::FONT_HERSHEY_SIMPLEX, scale, thick, &base);

        cv::putText(img, big,
                    cv::Point((w - sz.width) / 2, (h + sz.height) / 2),
                    cv::FONT_HERSHEY_SIMPLEX, scale, cv::Scalar(255, 255, 255),
                    thick, cv::LINE_AA);

        cv::imshow("clock", img);
        int key = cv::waitKey(1);
        if (key == 27 || key == 'q') break;
    }
    return 0;
}
