// =============================================================================
//  step2 故障注入练习（坏版本）：letterbox 正向 + 还原回原图 + 可视化对比。
//  构建/运行方法见同目录 README.md（需要板子，OpenCV 环境）。
//  现象与工作表：docs/teaching/step2-letterbox坐标还原/fault-injection.md
// =============================================================================
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <cstdio>
#include <algorithm>

struct LB
{
    float scale = 1.0f;
    int left = 0, right = 0, top = 0, bottom = 0;
};

// 正向：原图 -> 640×640 letterbox
static LB letterbox(const cv::Mat &src, cv::Mat &dst, int tgt = 640)
{
    LB lb;
    float sw = (float)tgt / (float)src.cols;
    float sh = (float)tgt / (float)src.rows;
    lb.scale = std::min(sw, sh);

    int nw = (int)(src.cols * lb.scale);
    int nh = (int)(src.rows * lb.scale);
    int dw = tgt - nw, dh = tgt - nh;
    lb.left = dw / 2;
    lb.top  = dh / 2;
    lb.right  = dw - lb.left;
    lb.bottom = dh - lb.top;

    cv::Mat scaled;
    cv::resize(src, scaled, cv::Size(nw, nh));
    cv::copyMakeBorder(scaled, dst, lb.top, lb.bottom, lb.left, lb.right,
                       cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));
    return lb;
}

// 反向：模型坐标 -> 原图坐标
static cv::Point2f to_original(const LB &lb, float x, float y)
{
    return cv::Point2f(x / lb.scale, y / lb.scale);
}

int main(int argc, char **argv)
{
    if (argc < 2) { printf("usage: %s <image> [out.png]\n", argv[0]); return 1; }

    cv::Mat img = cv::imread(argv[1]);
    if (img.empty()) { printf("imread fail: %s\n", argv[1]); return 1; }
    const char *out = (argc > 2) ? argv[2] : "restore_out.png";

    cv::Mat lbimg;
    LB lb = letterbox(img, lbimg, 640);
    printf("[img] %dx%d\n", img.cols, img.rows);
    printf("[lb] scale=%.6f pads(l=%d r=%d t=%d b=%d)\n",
           lb.scale, lb.left, lb.right, lb.top, lb.bottom);

    // 已知"球心"：取原图中心，半径 = min(w,h)×0.15（这就是正确位置）
    cv::Point2f c0(img.cols / 2.0f, img.rows / 2.0f);
    float r0 = std::min(img.cols, img.rows) * 0.15f;

    // 正向换算到模型坐标（正确步骤）
    float xm = c0.x * lb.scale + lb.left;
    float ym = c0.y * lb.scale + lb.top;

    // 还原回原图（怀疑这里有问题）
    cv::Point2f cb = to_original(lb, xm, ym);

    printf("correct  center = (%.1f, %.1f)\n", c0.x, c0.y);
    printf("restored center = (%.1f, %.1f)\n", cb.x, cb.y);
    printf("delta           = (%.1f, %.1f)\n", cb.x - c0.x, cb.y - c0.y);

    cv::Mat vis = img.clone();
    cv::circle(vis, c0, r0, cv::Scalar(0, 255, 0), 2);   // 绿：正确位置
    cv::circle(vis, cb, r0, cv::Scalar(0, 0, 255), 2);   // 红：还原后的位置
    if (!cv::imwrite(out, vis)) { printf("imwrite fail: %s\n", out); return 1; }
    printf("saved: %s\n", out);
    return 0;
}
