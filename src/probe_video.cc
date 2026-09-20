// =============================================================================
//  src/probe_video.cc  ——  视频解码探针（**诊断工具，不是学习内容**）
//
//  【为什么需要它】
//      main.cc 跑视频时会在随机的位置提前停止（39 / 57 / 70 / 72 帧都有），
//      而 `ffprobe -count_frames` 能解出全部帧。所以怀疑是：
//          cv::VideoCapture::read() 会**偶发返回 false**，
//          而我们的循环「见一次 false 就 break」-> 整个视频被腰斩。
//
//      这个探针不跑模型、不做任何推理，只做一件事：
//          **反复 read，失败也不立刻放弃，看看到底能解出多少帧。**
//
//  【用法】
//      ./probe_video <视频文件> [reuse|fresh] [最多连续失败次数=3]
//
//      reuse = 复用同一个 cv::Mat（默认，也是 main.cc 应该采用的写法）
//      fresh = 每帧新建一个 cv::Mat（模拟 main.cc 当前写法）
//
//  【看什么】
//      "解出 N 帧" 如果接近文件声称的帧数 -> 证明「偶发失败」假设成立
//      "最多连续失败 M 次" 告诉你：retry 的阈值设成几才够
// =============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <opencv2/videoio.hpp>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "用法: %s <视频文件> [reuse|fresh] [最多连续失败次数=3]\n", argv[0]);
        return -1;
    }

    const std::string path = argv[1];
    const bool   fresh  = (argc >= 3 && std::string(argv[2]) == "fresh");
    const int    maxfail = (argc >= 4) ? atoi(argv[3]) : 3;

    cv::VideoCapture cap;
    if (!cap.open(path))
    {
        fprintf(stderr, "打不开: %s\n", path.c_str());
        return -1;
    }

    const double total = cap.get(cv::CAP_PROP_FRAME_COUNT);
    fprintf(stderr, "---- probe_video ----\n");
    fprintf(stderr, "文件声称: %.0f 帧, %.0fx%.0f @ %.1f fps\n", total,
            cap.get(cv::CAP_PROP_FRAME_WIDTH),
            cap.get(cv::CAP_PROP_FRAME_HEIGHT),
            cap.get(cv::CAP_PROP_FPS));
    fprintf(stderr, "模式    : %s   连续失败上限 = %d\n",
            fresh ? "fresh（每帧新建 Mat）" : "reuse（复用同一个 Mat）", maxfail);

    cv::Mat shared;          // reuse 模式用
    int n = 0;               // 成功解出的帧数
    int consecutive = 0;     // 当前连续失败次数
    int max_consecutive = 0;
    int total_fail = 0;      // 累计失败次数

    while (true)
    {
        bool ok;
        if (fresh)
        {
            cv::Mat tmp;                       // 每帧新建 —— 模拟 main.cc 当前写法
            ok = cap.read(tmp);
            if (ok && tmp.empty()) ok = false;
        }
        else
        {
            ok = cap.read(shared);             // 复用同一个缓冲
            if (ok && shared.empty()) ok = false;
        }

        if (ok) { ++n; consecutive = 0; continue; }

        ++consecutive;
        ++total_fail;
        if (consecutive > max_consecutive) max_consecutive = consecutive;
        if (consecutive >= maxfail) break;     // 连续失败太多次，认定真结束了
    }

    fprintf(stderr, "------------------------------------------------------\n");
    fprintf(stderr, "解出帧数        : %d\n", n);
    fprintf(stderr, "累计失败次数    : %d\n", total_fail);
    fprintf(stderr, "最多连续失败    : %d  <- retry 阈值至少要设这么大\n", max_consecutive);
    fprintf(stderr, "文件声称帧数    : %.0f\n", total);
    if (total > 0)
    {
        fprintf(stderr, "覆盖率          : %.1f %%\n", 100.0 * n / total);
        if (n >= (int)total - 2)
            fprintf(stderr, "\n>> 假设成立：read() 会偶发失败，但不是真的到结尾。\n");
        else
            fprintf(stderr, "\n>> 假设不成立：确实解不出那么多帧，得换方向查。\n");
    }
    return 0;
}
