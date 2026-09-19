#include <stdio.h>
#include "rkYolov5s.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        printf("Usage: %s <model_path> <image_path>\n", argv[0]);
        return -1;
    }

    rkYolov5s model(argv[1]);
    if (model.init(nullptr, false) != 0)
    {
        printf("model init failed\n");
        return -1;
    }

    cv::Mat img = cv::imread(argv[2]);
    if (img.empty()) { printf("read image %s failed\n", argv[2]); return -1; }
    printf("image: %dx%d channels=%d\n", img.cols, img.rows, img.channels());

    // model.infer(img);

    for (int f = 0; f < 3; f++)
    {
        printf("--- frame %d ---\n", f);

        FrameResult r =model.infer(img);

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

    printf("B7-3 OK\n");
    return 0;

}
