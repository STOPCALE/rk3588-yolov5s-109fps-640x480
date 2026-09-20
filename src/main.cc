#include <stdio.h>
#include "rkYolov5s.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"
#include <stdlib.h>
#include <opencv2/core/utility.hpp>
#include <opencv2/video.hpp>

//计时系统，对实时系统进行判断的
struct StageStat
{
    double sum  = 0.0, mn = 0.0, mx = 0.0;
    int    n    = 0;

    void add(double v)
    {
        if (n == 0 || v < mn) mn = v;   //第一帧时初始化mn
        if (n == 0 || v > mx) mx = v;
        sum += v;
        ++n;
    }
    double avg() const { return n ? sum / n : 0.0; }
};

int main(int argc, char **argv)
{
    if (argc < 3 || argc >4)
    {
        printf("Usage: %s <model_path> <image_path> [frames=3]\n", argv[0]);
        return -1;
    }

    //第四个参数可选，循环多少次，不给就三次
    int max_frames = 3;
    if (argc == 4) max_frames = atoi(argv[3]);
    if (max_frames <= 0) max_frames = 3;    //防呆
    printf("frame = %d\n", max_frames);     //回显


    rkYolov5s model(argv[1]);
    if (model.init(nullptr, false) != 0)
    {
        printf("model init failed\n");
        return -1;
    }



    const double freq = cv::getTickFrequency();     //每秒多少tick
    StageStat st_read, st_infer;

    for (int f = 0; f < max_frames; f++)
    {
        printf("--- frame %d ---\n", f);

        //取一帧
        int64_t t = cv:: getTickCount();
        cv::Mat img = cv::imread(argv[2]);
        st_read.add((cv::getTickCount() - t) * 1000.0 / freq);

        if (img.empty()) { printf("read image %s failed\n", argv[2]); return -1; }
        if (f == 0) printf("image: %dx%d channels=%d\n", img.cols, img.rows, img.channels());

        //推理一帧
        t = cv::getTickCount();
        FrameResult r =model.infer(img);
        st_infer.add((cv::getTickCount() - t) * 1000.0 / freq);

        printf("检测到 %d 个球;\n", (int)r.balls.size());
        for (int k = 0; k < (int)r.balls.size(); k++)
        {
            const vb_ball_t &b = r.balls[k];
            printf(" [%d] cx=%.1f cy=%.1f r=%.1f prop=%.3f\n",
                    k, b.cx, b.cy, b.radius, b.prop);

            //画圆(绿)
            const cv::Point c((int)(b.cx + 0.5f), (int)(b.cy + 0.5f));
            cv::circle(img, c, (int)(b.radius + 0.5f), cv::Scalar(0, 255, 0), 3);
            //画出十字中心(红)
            cv::drawMarker(img, c, cv::Scalar(0, 0, 255), cv::MARKER_CROSS, 24, 2);
        }

        if (f == 0) //只存第一帧
        {
            const char *out = "/tmp/b7_result.jpg";
            if (cv::imwrite(out, img))  printf("已保存：%s\n", out);
            else                        printf("保存失败：%s\n", out);
        }
    }

    printf("\n=========== 汇总（每帧耗时 ms）===========\n");
    printf("  阶段        平均       最小       最大\n");
    printf("  read    %8.2f   %8.2f   %8.2f\n", st_read.avg(),  st_read.mn,  st_read.mx);
    printf("  infer   %8.2f   %8.2f   %8.2f\n", st_infer.avg(), st_infer.mn, st_infer.mx);
    printf("B7-3 OK\n");
    return 0;

}
