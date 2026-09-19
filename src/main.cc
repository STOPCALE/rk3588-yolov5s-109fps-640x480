#include <stdio.h>
#include "rkYolov5s.hpp"
#include "opencv2/imgcodecs.hpp"

int main(int argc, char **argv)
{
    if (argc != 2)
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

    model.infer(img);


    printf("B5 OK\n");
    return 0;

}
