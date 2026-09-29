#include <stdio.h>
#include "rkYolov5s.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"
#include <stdlib.h>
#include <string.h>
#include <opencv2/core/utility.hpp>
#include <opencv2/videoio.hpp>
#include <thread>
#include <atomic>
#include <deque>
#include <chrono>
#include <math.h>
#include <unistd.h>
#include <opencv2/highgui.hpp>

//计时系统
struct StageStat
{
    double sum = 0.0, mn = 0.0, mx = 0.0;
    int    n   = 0;

    void add(double v)
    {
        if (n == 0 || v < mn) mn = v;
        if (n == 0 || v > mx) mx = v;
        sum += v;
        ++n;
    }
    double avg() const { return n ? sum / n : 0.0;}
};

int main(int argc, char **argv)
{
    //参数检测
    if (argc < 2 || argc > 24)
    {
        printf("Usage: %s <model_path> <image_path|video|/dev/videoN> \n", argv[0]);
        return -1;
    }

    //模型读取
    rkYolov5s model(argv[1]);

    if (model.init(nullptr, false) != 0)
    {
        printf("model init failed\n");
        return -1;
    }

    //时间记录
    const double freq = cv::getTickFrequency();
    StageStat st_read, st_infer;

    //读取图片
    int64_t t = cv:: getTickCount();
    cv::Mat img;
    img = cv::imread(argv[2]);
    if (img.empty()) { printf("读不到图片：%s\n", argv[2]); return -1; }

    st_read.add((cv::getTickCount() - t) * 1000.0 / freq);

    printf("image: %dx%d channels=%d\n", img.cols, img.rows, img.channels());

    //推理一帧
    t = cv::getTickCount();
    FrameResult r = model.infer(img);
    st_infer.add((cv::getTickCount() - t) * 1000.0 / freq);

    printf("\n=========== 汇总（每帧耗时 ms）===========\n");
    printf("  阶段        平均       最小       最大\n");
    printf("  read    %8.2f   %8.2f   %8.2f\n", st_read.avg(),  st_read.mn,  st_read.mx);
    printf("  infer   %8.2f   %8.2f   %8.2f\n", st_infer.avg(), st_infer.mn, st_infer.mx);

    return 0;
}