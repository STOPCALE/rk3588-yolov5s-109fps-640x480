// =============================================================================
//  src/pool_selftest.cc —— rknnPool 自测工具（**诊断工具，不是学习内容**）
//
//  【为什么需要它】
//      rknnPool 是模板类。模板的成员函数在「被实例化」之前，编译器不做完整检查，
//      check.ps1 也照不到它 —— 这就是 `futs.get()` 这类错误能溜过去的原因。
//      本工具用「假模型」（不需要 NPU、不需要图片）把 put/get 整条路径**实例化**，
//      一次验证 3 个并发正确性修复：
//        T1 (P0-1) 提交后立刻覆写原图  → worker 必须读到「提交那一刻」的画面
//        T2 (P0-2) 慢推理 + 快提交 + maxPending=2 → 在途 ≤ 2，被拒的帧不被执行
//        T3 (P0-3) 一个线程阻塞在 get() 时，put() 不应被堵住
//
//  【在板子上编译 + 运行】
//      cd ~/myproj
//      g++ -O2 -std=c++14 -Iinclude src/pool_selftest.cc -o /tmp/pool_selftest \
//          $(pkg-config --cflags --libs opencv4) -pthread
//      taskset -c 4-7 /tmp/pool_selftest
//
//  【怎么读结果】三项全 PASS 时退出码 = 0。
// =============================================================================

#include "rknnPool.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

// ------------------------------- 假模型 -------------------------------

struct TestOut
{
    uint8_t  first = 0;     // 推理时刻图像的首字节
    uint32_t hash  = 0;     // 推理时刻图像的 FNV-1a 校验和
};

static std::atomic<int> g_exec{0};      // infer 实际执行次数（T2 的关键判据）
static std::atomic<int> g_inferMs{2};   // 模拟每次推理耗时

static uint32_t fnv1a(const cv::Mat &img)
{
    uint32_t h = 2166136261u;
    const int n = (int)(img.total() * img.channels());
    for (int i = 0; i < n; i++)
        h = (h ^ (uint8_t)img.data[i]) * 16777619u;
    return h;
}

struct FakeModel
{
    explicit FakeModel(const char *path) { (void)path; }
    int   init(void *ctx, bool dup) { (void)ctx; (void)dup; return 0; }
    void *get_pctx() { return this; }

    TestOut infer(cv::Mat &img)
    {
        ++g_exec;
        std::this_thread::sleep_for(std::chrono::milliseconds(g_inferMs.load()));

        TestOut o;
        o.first = img.data[0];
        o.hash  = fnv1a(img);
        return o;
    }
};

static void fill(cv::Mat &m, uint8_t v)
{
    memset(m.data, v, m.total() * m.channels());
}

// -------------------- T1：深拷贝（P0-1） --------------------

static bool test_clone()
{
    printf("== T1 (P0-1) 提交后立刻覆写原图（深拷贝验证）==\n");

    rknnPool<FakeModel, cv::Mat, TestOut> pool("fake", 3);
    if (pool.init() != 0) { printf("   pool.init 失败\n"); return false; }
    g_inferMs = 2;

    const int W = 640, H = 480;
    const int N = 100;
    int bad = 0;

    for (int i = 0; i < N; i++)
    {
        const uint8_t v = (uint8_t)(i % 250 + 1);   // 每帧一个独特值（1~100）

        cv::Mat img(H, W, CV_8UC3);
        fill(img, v);
        const uint32_t expect = fnv1a(img);          // 「提交那一刻」的期望值

        pool.put(img);                                // 提交（内部应深拷贝）
        fill(img, 0xAA);                              // ★ 立刻覆写同一块像素内存

        TestOut o;
        if (pool.get(o) != 0) { printf("   get 失败\n"); return false; }
        if (o.first != v || o.hash != expect) bad++;
    }

    printf("   帧数=%d  不一致=%d  -> %s\n\n", N, bad, bad == 0 ? "PASS" : "FAIL");
    return bad == 0;
}

// -------------------- T2：背压（P0-2） --------------------

static bool test_backpressure()
{
    printf("== T2 (P0-2) 背压：慢推理 20ms + 快提交 30 帧 + maxPending=2 ==\n");

    rknnPool<FakeModel, cv::Mat, TestOut> pool("fake", 3);
    if (pool.init() != 0) { printf("   pool.init 失败\n"); return false; }
    g_inferMs = 20;
    pool.setMaxPending(2);

    cv::Mat img(480, 640, CV_8UC3);
    fill(img, 7);

    const int N = 30;
    int accepted = 0, rejected = 0;
    size_t max_seen = 0;

    const int before = g_exec.load();
    for (int i = 0; i < N; i++)
    {
        int r = pool.put(img);                       // 0=收下  1=拒收
        if (r == 0) accepted++; else rejected++;
        max_seen = std::max(max_seen, pool.pending());
    }
    const int exec_burst = g_exec.load() - before;

    TestOut o; int got = 0;                          // 收结果
    while (pool.get(o) == 0) got++;
    const int exec_total = g_exec.load() - before;

    printf("   提交 %d 帧： accept=%d  reject=%d  pending 峰值=%zu\n",
           N, accepted, rejected, max_seen);
    printf("   infer 实际执行=%d（突发期 %d）  取回结果=%d  dropped()=%zu\n",
           exec_total, exec_burst, got, pool.dropped());

    const bool ok = (accepted == 2) && (rejected == 28) && (max_seen <= 2) &&
                    (exec_total == 2) && (got == 2);
    printf("   -> %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

// --------------- T3：get() 阻塞时不堵 put()（P0-3） ---------------

static bool test_get_lock()
{
    printf("== T3 (P0-3) 一个线程阻塞在 get() 时 put() 的耗时 ==\n");

    rknnPool<FakeModel, cv::Mat, TestOut> pool("fake", 2);
    if (pool.init() != 0) { printf("   pool.init 失败\n"); return false; }
    g_inferMs = 40;

    cv::Mat img(480, 640, CV_8UC3);
    fill(img, 3);

    pool.put(img);                                   // 第 1 帧开始推理（约 40ms）

    std::atomic<bool> a_started{false};
    std::thread a([&]()
    {
        a_started = true;
        TestOut o;
        pool.get(o);                                 // 阻塞等待第 1 帧结果
    });
    while (!a_started) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(3));   // 让 A 先拿到锁

    const auto t0 = std::chrono::steady_clock::now();
    pool.put(img);                                   // 第 2 帧 —— 不应被 A 的等待堵住
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    a.join();
    TestOut o;
    pool.get(o);                                     // 收第 2 帧的结果

    const bool ok = ms < 5.0;
    printf("   put() 耗时 = %.3f ms（对照：修复前会被堵 ≈ 剩余推理时间 30ms+）-> %s\n\n",
           ms, ok ? "PASS" : "FAIL");
    return ok;
}

int main()
{
    printf("#################### rknnPool 自测 ####################\n\n");
    const bool t1 = test_clone();
    const bool t2 = test_backpressure();
    const bool t3 = test_get_lock();
    printf("#################### 汇总 ####################\n");
    printf("   T1 深拷贝   : %s\n", t1 ? "PASS" : "FAIL");
    printf("   T2 背压     : %s\n", t2 ? "PASS" : "FAIL");
    printf("   T3 锁外等待 : %s\n", t3 ? "PASS" : "FAIL");
    return (t1 && t2 && t3) ? 0 : 1;
}
