// =============================================================================
//  src\main.cc  ——  B8-1  单线程「取帧 -> 推理 -> 画框 -> 输出」流水线
//
//  【为什么要把 main.cc 重写】
//      B7 之前的 main.cc 只会读一张静态图跑 3 次，那是"看单张照片"的测试方式。
//      真实系统里图像是"连续来"的：一帧接一帧，永远不停。所以这里换成循环，
//      这个结构就叫流水线(pipeline)，它由四个阶段组成：
//
//          取帧(read) -> 推理(infer) -> 画框(draw) -> 输出(write)
//
//      B8-1 先用最笨的方式把它串起来：四个阶段挤在同一个线程里排队做。
//      这样做的好处是逻辑简单、结果可信；坏处是"慢的那个拖死快的那个"。
//      我们要先用它测出一条基线（串行到底能跑多少帧），
//      B8-3 再拆成三个线程，就能定量看出解耦到底赚了多少 fps。
//
//  【输入的三种形态】看路径自动判断，不需要额外参数
//      目录                  -> 逐张图片处理（批量验证用，比如整个 val 集）
//      *.jpg *.png *.bmp ...  -> 单张图片，默认跑 3 次（沿用 B7 的老习惯，看结果稳不稳）
//      *.mp4 *.avi *.mkv ...  -> 视频，逐帧处理（手机录一段就能拿来验证）
//
//  【用法】
//      ./my_rknn_yolov5_demo <model.rknn> <输入路径> [选项]
//
//  【选项】
//      --quiet          关掉每帧的调试打印（★ 测 fps 一定加，否则测的是打印速度）
//                       注意：--quiet 只关"调试打印"，[result] 那行结果照旧打印，
//                       因为那是产出，不是调试信息。
//      --max N          最多处理 N 帧
//      --out <路径>     写结果：视频->mp4 文件；目录->该目录下每张 jpg；单图->该文件
//                       不给 --out 就只测速度、不写任何文件（默认，最干净）
//
//  【例子】
//      # 用手机录的视频验证效果，并把标注结果存成 mp4 拉回来看
//      ./my_rknn_yolov5_demo ./model/RK3588/best.rknn ~/testimg/vb.mp4 --out /tmp/out.mp4
//
//      # 测纯推理速度（--quiet 必加）
//      ./my_rknn_yolov5_demo ./model/RK3588/best.rknn ~/testimg/vb.mp4 --quiet
//
//      # 批量跑一个图片文件夹，结果给 analyze-val.ps1 统计精度
//      ./my_rknn_yolov5_demo ./model/RK3588/best.rknn ~/testimg/val --quiet > /tmp/valres.txt
//
//  【退出时打印的耗时表】每一列都是"每帧平均"，单位 ms
//      read   取一帧          （imread 或 cap.read）
//      infer  推理一帧        （预处理 + NPU + 后处理，也就是 model.infer() 整个的耗时）
//      draw   画框            （circle / drawMarker / putText）
//      write  写出去          （imwrite 或 VideoWriter::write）
//      实测帧率 = 总帧数 / 总墙钟时间，这是唯一不能造假的数字。
// =============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string>
#include <vector>
#include <algorithm>
#include <sys/stat.h>            // stat() / S_ISDIR  —— 判断路径是文件还是目录

#include "rkYolov5s.hpp"
#include "opencv2/core.hpp"      // cv::glob —— 列目录里的文件
#include "opencv2/imgcodecs.hpp" // imread / imwrite
#include "opencv2/imgproc.hpp"   // circle / drawMarker / putText
#include "opencv2/videoio.hpp"   // VideoCapture / VideoWriter

// -----------------------------------------------------------------------------
// 小工具：取路径里最后一段（"a/b/c.jpg" -> "c.jpg"）
//   视频模式下没有文件名，我们得自己造一个，所以统一走这个函数。
// -----------------------------------------------------------------------------
static std::string base_name(const std::string &path)
{
    const size_t s = path.find_last_of("/\\");
    return (s == std::string::npos) ? path : path.substr(s + 1);
}

// -----------------------------------------------------------------------------
// 小工具：取小写扩展名（".JPG" 和 ".jpg" 要当成一回事）
// -----------------------------------------------------------------------------
static std::string lower_ext(const std::string &path)
{
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return std::string();
    std::string e = path.substr(dot);
    for (size_t i = 0; i < e.size(); ++i)
        e[i] = (char)tolower((unsigned char)e[i]);
    return e;
}

// 按扩展名判断是不是图片（cv::imread 能读的那些）
static bool is_image_file(const std::string &path)
{
    const std::string e = lower_ext(path);
    return e == ".jpg"  || e == ".jpeg" || e == ".png" || e == ".bmp" ||
           e == ".webp" || e == ".tif"  || e == ".tiff";
}

// -----------------------------------------------------------------------------
// 计时统计小结构：一边跑一边攒 sum / min / max，最后一次性打印
//   为什么要 min 和 max？平均值会把"偶尔卡一下"抹掉。实时系统里最怕的就是
//   那"偶尔一次"的 200ms —— 球已经飞过去了。所以最大值得单独看。
// -----------------------------------------------------------------------------
struct StageStat
{
    double sum = 0.0, mn = 0.0, mx = 0.0;
    int    n   = 0;

    void add(double v)
    {
        if (n == 0 || v < mn) mn = v;   // 第一帧时顺手把 mn 初始化掉，省得写哨兵值
        if (n == 0 || v > mx) mx = v;
        sum += v;
        ++n;
    }
    double avg() const { return n ? sum / n : 0.0; }
};

static void print_stat_row(const char *tag, const StageStat &s)
{
    printf("  %-8s   %8.2f   %8.2f   %8.2f\n", tag, s.avg(), s.mn, s.mx);
}

static void usage(const char *exe)
{
    printf(
        "用法: %s <model.rknn> <输入路径> [选项]\n"
        "\n"
        "  <输入路径> 可以填三种东西（看路径自动判断）:\n"
        "      目录        例: ~/testimg/val        逐张图片处理（批量验证）\n"
        "      图片文件    例: ~/testimg/vb1.jpg    单张跑 3 次（看结果稳不稳）\n"
        "      视频文件    例: ~/testimg/vb.mp4     逐帧处理（手机录的视频）\n"
        "\n"
        "  选项:\n"
        "      --quiet        关掉每帧调试打印（测 fps 必加），[result] 结果行照旧打印\n"
        "      --max N        最多处理 N 帧\n"
        "      --out <路径>   视频->mp4文件; 目录->该目录下每张jpg; 单图->该文件\n"
        "                     不给 --out 就只测速度、不写文件\n"
        "\n"
        "  例子:\n"
        "      %s ./model/RK3588/best.rknn ~/testimg/vb.mp4 --quiet\n"
        "      %s ./model/RK3588/best.rknn ~/testimg/vb.mp4 --out /tmp/out.mp4\n"
        "      %s ./model/RK3588/best.rknn ~/testimg/val --quiet > /tmp/valres.txt\n",
        exe, exe, exe, exe);
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        usage(argv[0]);
        return -1;
    }

    const std::string model_path = argv[1];
    const std::string input      = argv[2];

    std::string out_path;
    int  max_frames = 0;        // 0 = 不限
    bool quiet      = false;
    bool user_max   = false;

    // ---------------------------------------------------------------- 解析选项
    for (int i = 3; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--quiet")
        {
            quiet = true;
        }
        else if (a == "--max" && i + 1 < argc)
        {
            max_frames = atoi(argv[++i]);
            user_max   = true;
        }
        else if (a == "--out" && i + 1 < argc)
        {
            out_path = argv[++i];
        }
        else
        {
            printf("未知选项: %s\n\n", a.c_str());
            usage(argv[0]);
            return -1;
        }
    }

    // ------------------------------------------------------------ 0. 初始化模型
    //   注意 init() 里的打印（模型属性、绑核、版本号）是一次性的，不受 --quiet 影响。
    rkYolov5s model(model_path);
    if (model.init(nullptr, false) != 0)
    {
        printf("model init failed\n");
        return -1;
    }
    model.set_verbose(!quiet);

    // ------------------------------------------------------------ 1. 看输入是什么
    struct stat st;
    const bool is_dir = (stat(input.c_str(), &st) == 0) && S_ISDIR(st.st_mode);

    std::vector<std::string> files;        // 图片模式 / 目录模式用（列成一张表挨个取）
    cv::VideoCapture         cap;          // 视频模式用
    bool                     use_video = false;
    bool                     repeat_one = false;   // 单图模式：同一个文件反复跑

    if (is_dir)
    {
        cv::glob(input + "/*", files, false);

        // cv::glob 会把目录里的所有东西都收进来（连 .txt 标签文件都不放过），
        // 所以这里按扩展名筛一遍，只留真图片。
        std::vector<std::string> keep;
        for (size_t i = 0; i < files.size(); ++i)
            if (is_image_file(files[i])) keep.push_back(files[i]);
        files.swap(keep);

        // 排序：让每次跑的先后顺序完全一致。调试时"上一次和这一次的日志对不上"
        // 是最浪费时间的事，而 glob 返回的顺序并不保证稳定。
        std::sort(files.begin(), files.end());

        if (files.empty()) { printf("目录里没找到图片: %s\n", input.c_str()); return -1; }
        printf("---- 目录模式: %s  共 %d 张图 ----\n", input.c_str(), (int)files.size());
    }
    else if (is_image_file(input))
    {
        files.push_back(input);
        repeat_one = true;
        // 单张图默认跑 3 次：B7-3 就是这么用的，重复几次能看出结果稳不稳。
        if (!user_max) max_frames = 3;
        printf("---- 图片模式: %s ----\n", input.c_str());
    }
    else
    {
        // 既不是目录也不是图片 -> 当视频试。这一步同时充当"路径打错了"的兜底检查。
        if (!cap.open(input))
        {
            printf("打不开输入: %s\n", input.c_str());
            printf("（它不是目录，不是认识的图片扩展名，也不是能解码的视频）\n");
            return -1;
        }
        use_video = true;

        printf("---- 视频模式: %s ----\n", input.c_str());
        printf("     分辨率 %dx%d  帧率 %.1f  总帧数 %.0f\n",
               (int)cap.get(cv::CAP_PROP_FRAME_WIDTH),
               (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT),
               cap.get(cv::CAP_PROP_FPS),
               cap.get(cv::CAP_PROP_FRAME_COUNT));

        // ---- B8-2: 把采集缓冲压到 1 帧 ----
        //   读文件时这行基本是空操作，但换成摄像头(cv::CAP_V4L2)才有意义：
        //   V4L2 默认缓冲常常是 4~5 帧，30fps 下就是 130~170ms 的"陈旧延迟"——
        //   你处理的是半秒前拍到的画面，球早就飞走了。压到 1 就只留最新那帧。
        //   提前写上，摄像头到位后不用再改代码。
        cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
    }

    // ------------------------------------------------------------ 2. 准备输出（可选）
    const bool       want_out = !out_path.empty();
    const std::string out_dir = (is_dir && want_out) ? out_path : std::string();

    cv::VideoWriter vw;
    if (use_video && want_out)
    {
        double ofps = cap.get(cv::CAP_PROP_FPS);
        // 有些视频帧率读出来是 0 或离谱值，给个兜底，否则 VideoWriter 直接开不起来
        if (ofps < 1.0 || ofps > 240.0) ofps = 30.0;

        const cv::Size osz((int)cap.get(cv::CAP_PROP_FRAME_WIDTH),
                           (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT));

        // fourcc 'mp4v' = MPEG-4 Part 2。OpenCV 自带的 FFmpeg 后端直接就能写，
        // 不用另外装编码器。（'H264' / 'avc1' 依赖系统里的 openh264，板子上一般没有）
        if (!vw.open(out_path, cv::VideoWriter::fourcc('m','p','4','v'), ofps, osz))
            printf("[warn] 视频输出打不开: %s（这次只测速度，不写文件）\n", out_path.c_str());
        else
            printf("     标注视频 -> %s\n", out_path.c_str());
    }

    // ------------------------------------------------------------ 3. 主循环
    StageStat st_read, st_infer, st_draw, st_write;
    // 用 int 不用 long long：MSVCRT 的 printf 不认 %lld（MinGW 会警告，Linux 无此问题）。
    // 球数不可能到 21 亿，int 完全够用，还能两边都干净。
    int total_balls = 0;
    int n_frames = 0, n_written = 0, n_write_fail = 0;

    const double freq   = cv::getTickFrequency();   // 每秒多少个 tick（板子上是 24MHz）
    const int64_t t_all0 = cv::getTickCount();

    for (int idx = 0; ; ++idx)
    {
        if (max_frames > 0 && idx >= max_frames) break;

        std::string name;
        cv::Mat     frame;
        int64_t     t;

        // ---- 3.1 取一帧 ----
        t = cv::getTickCount();
        if (use_video)
        {
            if (!cap.read(frame) || frame.empty()) break;   // 读到结尾或读失败就收工
            char nb[32];
            snprintf(nb, sizeof(nb), "f%06d", idx);         // 视频没文件名，自己造一个
            name = nb;
        }
        else
        {
            // 目录模式：表里跑完了就停。
            // 单图模式：表里只有 1 个元素，要跑 N 次就得取模，让同一个文件名反复出场。
            if (!repeat_one && idx >= (int)files.size()) break;

            name  = files[idx % files.size()];
            frame = cv::imread(name);
            if (frame.empty()) { printf("[skip] 读不到: %s\n", name.c_str()); continue; }
        }
        st_read.add((cv::getTickCount() - t) * 1000.0 / freq);

        // ---- 3.2 推理一帧（预处理 + NPU + 后处理都在里面）----
        t = cv::getTickCount();
        FrameResult r = model.infer(frame);
        st_infer.add((cv::getTickCount() - t) * 1000.0 / freq);

        // ---- 3.3 画框 ----
        t = cv::getTickCount();
        for (size_t k = 0; k < r.balls.size(); ++k)
        {
            const vb_ball_t &b = r.balls[k];
            const cv::Point  c((int)(b.cx + 0.5f), (int)(b.cy + 0.5f));   // +0.5 是四舍五入
            cv::circle(frame, c, (int)(b.radius + 0.5f), cv::Scalar(0, 255, 0), 3);    // 绿圈=半径
            cv::drawMarker(frame, c, cv::Scalar(0, 0, 255), cv::MARKER_CROSS, 24, 2);  // 红十字=圆心
        }

        // 视频模式的 HUD：录出来的 mp4 上直接能看到帧号，方便人眼对着原视频核对
        if (use_video)
        {
            char hud[128];
            snprintf(hud, sizeof(hud), "f=%d balls=%d", idx, (int)r.balls.size());
            cv::putText(frame, hud, cv::Point(10, 40), cv::FONT_HERSHEY_SIMPLEX,
                        1.2, cv::Scalar(0, 255, 255), 3);
        }
        st_draw.add((cv::getTickCount() - t) * 1000.0 / freq);

        // ---- 3.4 输出（给了 --out 才做）----
        if (want_out)
        {
            t = cv::getTickCount();
            if (use_video)
            {
                if (vw.isOpened()) { vw.write(frame); ++n_written; }
            }
            else
            {
                const std::string dst = out_dir.empty()
                                      ? out_path                              // 单图：当成文件名
                                      : (out_dir + "/" + base_name(name));    // 目录：当成目录
                if (cv::imwrite(dst, frame)) ++n_written;
                else                         ++n_write_fail;
            }
            st_write.add((cv::getTickCount() - t) * 1000.0 / freq);
        }

        // ---- 3.5 机器可读的一行结果 ----
        //   格式固定为:   [result] <名字> balls=<n> [cx=.. cy=.. r=.. prop=..]*
        //   字段顺序不能改，因为 analyze-val.ps1 靠这一行统计精度。
        //   注意：图片/目录模式即使加了 --quiet 也照常打印——
        //   它是产出，不是调试信息。（视频模式帧太多，--quiet 时才不打印）
        if (!use_video || !quiet)
        {
            printf("[result] %s balls=%d", base_name(name).c_str(), (int)r.balls.size());
            for (size_t k = 0; k < r.balls.size(); ++k)
                printf(" cx=%.1f cy=%.1f r=%.1f prop=%.3f",
                       r.balls[k].cx, r.balls[k].cy, r.balls[k].radius, r.balls[k].prop);
            printf("\n");
        }

        total_balls += (int)r.balls.size();
        ++n_frames;
    }

    const double ms_all = (cv::getTickCount() - t_all0) * 1000.0 / freq;

    if (vw.isOpened())  vw.release();     // VideoWriter 必须 release 才会写 mp4 的结尾索引，
    if (cap.isOpened()) cap.release();    // 不写索引的话文件能打开但播不出来

    // ------------------------------------------------------------ 4. 汇总
    printf("\n================ 汇总 ================\n");
    printf("处理帧数      : %d\n", n_frames);
    printf("检出球总数    : %d   (平均 %.2f 个/帧)\n", total_balls,
           n_frames ? (double)total_balls / n_frames : 0.0);
    if (want_out)
        printf("写出          : 成功 %d  失败 %d\n", n_written, n_write_fail);

    printf("\n--- 各段耗时 ms/帧 ---\n");
    printf("  阶段           平均       最小       最大\n");
    print_stat_row("read",  st_read);
    print_stat_row("infer", st_infer);
    print_stat_row("draw",  st_draw);
    print_stat_row("write", st_write);

    printf("\n总墙钟耗时    : %.1f ms\n", ms_all);
    if (ms_all > 0.0 && n_frames > 0)
        printf("★ 实测帧率    : %.2f fps\n", n_frames * 1000.0 / ms_all);

    if (use_video)
    {
        printf("\n注意: 视频文件是「本地解码」，不走摄像头驱动，所以 read 那一列\n"
               "      并不代表真实采集耗时。接上摄像头后它会明显变大（要等曝光、\n"
               "      要等 USB 传输）。真正可信的是 infer 那一列。\n");
    }

    return 0;
}
