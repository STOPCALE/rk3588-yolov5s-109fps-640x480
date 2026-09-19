#include <stdio.h>
#include <stdlib.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>
#include "rkYolov5s.hpp"
#include "opencv2/imgcodecs.hpp"

// ============================================================================
//  B6.6  ——  3 实例并行吞吐测试（临时程序，测完可删）
//
//  用法:  ./my_rknn_yolov5_demo <model.rknn> <image> <秒数>
//
//  三种配置（用环境变量切换）:
//    1) 默认                  → 3 个独立实例，绑 3 个不同核      【吞吐上限】
//    2) SHARE_WEIGHT=1        → 3 个实例 dup_context 共享权重   【rknnPool 方案】
//    3) RKNN_CORE_MASK=1      → 3 个实例全挤在核 0              【单核对照】
// ============================================================================
int main(int argc, char **argv)
{
    if (argc != 4)
    {
        printf("Usage: %s <model_path> <image_path> <seconds>\n", argv[0]);
        return -1;
    }

    const int N       = 3;                          //实例数 = NPU 核数
    const int seconds = atoi(argv[3]);
    if (seconds <= 0) { printf("seconds must be > 0\n"); return -1; }

    const bool share = (getenv("SHARE_WEIGHT") != nullptr);

    cv::Mat img0 = cv::imread(argv[2]);
    if (img0.empty()) { printf("read image %s failed\n", argv[2]); return -1; }

    printf("=== B6.6  3实例并行吞吐测试 ===\n");
    printf("模型=%s  图=%dx%d  时长=%ds  实例数=%d  共享权重=%s\n",
           argv[1], img0.cols, img0.rows, seconds, N, share ? "是" : "否");

    //---------- 创建 N 个实例 ----------
    // 注意: 不要设置 RKNN_CORE_MASK, 否则会覆盖 get_core_num() 的轮转绑核
    std::vector< std::unique_ptr<rkYolov5s> > models;
    for (int i = 0; i < N; i++)
    {
        models.emplace_back(std::make_unique<rkYolov5s>(argv[1]));
        int ret = (i == 0 || !share)
                    ? models[i]->init(nullptr, false)
                    : models[i]->init(models[0]->get_pctx(), true);
        if (ret != 0) { printf("[FAIL] model[%d] init failed\n", i); return -1; }
    }

    //---------- 启动 N 个 worker ----------
    // 每个 worker 只写自己那一格, 所以不需要原子变量
    long              counts[N] = {0};
    double            tsum[N]   = {0.0};
    std::atomic<bool> go(false);
    std::atomic<bool> stop(false);

    std::vector<std::thread> workers;
    for (int i = 0; i < N; i++)
    {
        // my_img = img0.clone(): 每个 worker 一份独立的像素内存, 避免数据竞争
        workers.emplace_back([&, i, my_img = img0.clone()]() mutable
        {
            while (!go.load(std::memory_order_relaxed))
                std::this_thread::yield();

            while (!stop.load(std::memory_order_relaxed))
            {
                auto t0 = std::chrono::steady_clock::now();
                models[i]->infer(my_img);
                auto t1 = std::chrono::steady_clock::now();
                counts[i]++;
                tsum[i] += std::chrono::duration<double, std::milli>(t1 - t0).count();
            }
        });
    }

    //---------- 计时窗口: 所有 worker 就绪后统一起跑 ----------
    auto t_start = std::chrono::steady_clock::now();
    go.store(true);
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    stop.store(true);
    for (auto &w : workers) w.join();
    auto t_end = std::chrono::steady_clock::now();

    const double wall  = std::chrono::duration<double>(t_end - t_start).count();
    const double fps   = (double)0;
    (void)fps;

    //---------- 汇总 ----------
    long total = 0;
    printf("\n=== 每实例统计 ===\n");
    for (int i = 0; i < N; i++)
    {
        double avg = (counts[i] > 0) ? tsum[i] / (double)counts[i] : 0.0;
        printf("worker[%d]: 帧数=%-6ld  平均infer=%.2f ms  (%.1f fps)\n",
               i, counts[i], avg, (avg > 0.0) ? 1000.0 / avg : 0.0);
        total += counts[i];
    }

    double throughput = (double)total / wall;
    printf("\n=== 汇总 ===\n");
    printf("墙钟=%.3f s   总帧数=%ld\n", wall, total);
    printf(">>> THROUGHPUT        = %.1f fps <<<\n", throughput);
    printf(">>> 单帧服务时间(延迟下限) = %.2f ms <<<\n", 1000.0 * N / throughput);
    printf(">>> 判定: 90fps %s   110fps %s <<<\n",
           (throughput >= 90.0)  ? "达标 OK " : "未达标 NO",
           (throughput >= 110.0) ? "达标 OK " : "未达标 NO");
    return 0;
}