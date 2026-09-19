// =============================================================================
//  src/bench_throughput.cc  ——  多实例并行吞吐基准（**回归验证工具，不是学习内容**）
//
//  【为什么需要它】
//      B6.6 测到 120.3 fps，得出"90/110 fps 双达标"的结论。
//      但后来发现当时 **dmc（内存控制器）没有定频**，而 dmc 会直接影响 run：
//          528 MHz -> run = 27.6 ms
//          2112 MHz -> run = 22.4 ms   (-19%)
//      所以那个 120.3 fps 是在"不知道 dmc 哪一档"的情况下测的，必须重测。
//
//  【它是怎么测的】
//      1. 建 N 个实例，每个绑一个 NPU 核（get_core_num() 轮转 0/1/2）
//      2. N 个线程，每线程自己循环跑 frames 次 infer()
//      3. 吞吐 = 总帧数 / 墙钟时间；同时报告每个 worker 的 infer 分布
//
//      ⚠️ 关键设计：**每个线程独立循环**，不是"主线程发任务、worker 收"。
//         后者会把主线程的开销混进来，测不出纯推理吞吐。
//
//  【用法】
//      ./bench_throughput <model.rknn> <image> [线程数=3] [每线程帧数=300] [dup|indep]
//
//      indep = 每个实例各自 rknn_init（独立权重，占内存多）
//      dup   = 第一个 rknn_init，其余 rknn_dup_context（共享权重，默认）
//
//  【跑之前必须先做】（否则数据不可信，见 docs/开发流程与常用指令.md §0.2.1）
//      sudo bash ~/myproj/tools/lock-freq.sh lock          # 锁频（含 dmc！）
//      taskset -c 4-7 ./bench_throughput ...               # 绑大核
//
//  【两组对照实验】
//      正常：        ./bench_throughput model img 3 300 dup
//      全挤核0：     RKNN_CORE_MASK=1 ./bench_throughput model img 3 300 dup
//      ↑ 第二组是**验证测试工具本身**用的：B6.6 当时测到 41.9 fps，
//        如果这次也是 ~42 fps，说明两套工具有可比性。
//
//  注：所有逐帧 printf 会被重定向到 /dev/null ——
//      打印开销会污染测量（printf 走 SSH 管道时每帧能吃掉 1~2 ms）。
//      结果用 fprintf(stderr) 输出，不受影响。
// =============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <thread>
#include <algorithm>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/core/utility.hpp>

#include "rkYolov5s.hpp"

struct Stat
{
    double sum = 0.0, mn = 0.0, mx = 0.0;
    int    n   = 0;
    void add(double v)
    {
        if (n == 0 || v < mn) mn = v;
        if (n == 0 || v > mx) mx = v;
        sum += v; ++n;
    }
    double avg() const { return n ? sum / n : 0.0; }
};

static void usage(const char *exe)
{
    fprintf(stderr,
        "用法: %s <model.rknn> <image> [线程数=3] [每线程帧数=300] [dup|indep]\n"
        "\n"
        "  例: %s ./model/RK3588/best.rknn ~/testimg/vb640.jpg 3 300 dup\n"
        "  对照: RKNN_CORE_MASK=1 %s ./model/RK3588/best.rknn ~/testimg/vb640.jpg 3 300 dup\n",
        exe, exe, exe);
}

int main(int argc, char **argv)
{
    if (argc < 3) { usage(argv[0]); return -1; }

    const std::string model_path = argv[1];
    const std::string image_path = argv[2];
    int  n_threads = (argc >= 4) ? atoi(argv[3]) : 3;
    int  frames    = (argc >= 5) ? atoi(argv[4]) : 300;
    bool use_dup   = (argc >= 6) ? (std::string(argv[5]) == "dup") : true;

    if (n_threads < 1) n_threads = 1;
    if (n_threads > 16) n_threads = 16;
    if (frames < 1) frames = 1;

    // ---- 读图（只读一次，所有线程共享同一块像素；infer 只读它，不改）----
    cv::Mat img = cv::imread(image_path);
    if (img.empty()) { fprintf(stderr, "读不到图: %s\n", image_path.c_str()); return -1; }

    const char *env_mask = getenv("RKNN_CORE_MASK");

    fprintf(stderr, "==================== bench_throughput ====================\n");
    fprintf(stderr, "模型      : %s\n", model_path.c_str());
    fprintf(stderr, "图片      : %s  (%dx%d)\n", image_path.c_str(), img.cols, img.rows);
    fprintf(stderr, "实例数    : %d   (%s)\n", n_threads,
            use_dup ? "dup_context 共享权重" : "各自 rknn_init 独立权重");
    fprintf(stderr, "每实例帧数: %d   (总共 %d 帧)\n", frames, n_threads * frames);
    fprintf(stderr, "核绑定    : %s\n",
            env_mask ? (std::string("RKNN_CORE_MASK=") + env_mask + "  <- 环境变量强制").c_str()
                     : "get_core_num() 轮转 0/1/2（看下面 init 打印的 mask）");
    fprintf(stderr, "----------------------------------------------------------\n");

    // ---- 1. 建实例（必须在主线程串行建：dup_context 要求源 ctx 已就绪）----
    std::vector<rkYolov5s *> inst(n_threads, nullptr);
    for (int k = 0; k < n_threads; ++k)
    {
        inst[k] = new rkYolov5s(model_path);
        int ret = (k == 0 || !use_dup)
                ? inst[k]->init(nullptr, false)                 // 独立初始化
                : inst[k]->init(inst[0]->get_pctx(), true);     // 共享权重
        if (ret != 0) { fprintf(stderr, "实例 %d 初始化失败\n", k); return -1; }
    }

    // ---- 2. 从这行之后，逐帧调试打印全部丢掉（/dev/null）----
    //        注意：init() 的打印在上面，那些是有用的（能看到每个实例绑了哪个核）
    if (!freopen("/dev/null", "w", stdout))
        fprintf(stderr, "[warn] freopen /dev/null 失败，打印开销会混进测量\n");
    fprintf(stderr, "（以下逐帧调试输出已丢弃到 /dev/null）\n\n");

    // ---- 3. 开跑：每个线程独立循环，互不通信 ----
    std::vector<std::thread> th;
    std::vector<Stat>        per(n_threads);
    std::vector<int>         cnt(n_threads, 0);

    const double  freq   = cv::getTickFrequency();
    const int64_t t_all0 = cv::getTickCount();

    for (int k = 0; k < n_threads; ++k)
    {
        th.emplace_back([&, k]()
        {
            for (int f = 0; f < frames; ++f)
            {
                const int64_t t = cv::getTickCount();
                inst[k]->infer(img);
                per[k].add((cv::getTickCount() - t) * 1000.0 / freq);
                ++cnt[k];
            }
        });
    }
    for (size_t i = 0; i < th.size(); ++i) th[i].join();

    const double total_ms = (cv::getTickCount() - t_all0) * 1000.0 / freq;

    // ---- 4. 汇总 ----
    int total = 0;
    for (int k = 0; k < n_threads; ++k) total += cnt[k];

    fprintf(stderr, "\n================ 结果 ================\n");
    fprintf(stderr, "%-8s %10s %10s %10s %8s\n", "worker", "avg(ms)", "min(ms)", "max(ms)", "帧数");
    for (int k = 0; k < n_threads; ++k)
        fprintf(stderr, "%-8d %10.2f %10.2f %10.2f %8d\n",
                k, per[k].avg(), per[k].mn, per[k].mx, cnt[k]);

    double sum_all = 0.0, mn_all = 0.0, mx_all = 0.0;
    for (int k = 0; k < n_threads; ++k)
    {
        sum_all += per[k].sum;
        if (k == 0 || per[k].mn < mn_all) mn_all = per[k].mn;
        if (k == 0 || per[k].mx > mx_all) mx_all = per[k].mx;
    }

    fprintf(stderr, "------------------------------------------------------\n");
    fprintf(stderr, "单帧平均 (全部 worker 平均): %.2f ms   [min %.2f / max %.2f]\n",
            sum_all / total, mn_all, mx_all);
    fprintf(stderr, "总墙钟  : %.1f ms\n", total_ms);
    fprintf(stderr, "总帧数  : %d\n", total);
    fprintf(stderr, "\n★ 吞吐 = %d 帧 / %.3f s = %.1f fps\n",
            total, total_ms / 1000.0, total * 1000.0 / total_ms);

    // 理论单实例上限（仅供参考）
    const double one_ms = sum_all / total;
    fprintf(stderr, "\n  对照：若单实例是 %.2f ms，则 1 个 worker 上限 %.1f fps，"
                    "%d 个理想并行应为 %.1f fps\n",
            one_ms, 1000.0 / one_ms, n_threads, n_threads * 1000.0 / one_ms);
    fprintf(stderr, "  若实测吞吐明显低于上面那个数 -> 说明 worker 之间在抢资源"
                    "（NPU 核 / 内存带宽 / 大核数量）\n");

    for (int k = 0; k < n_threads; ++k) delete inst[k];
    return 0;
}
