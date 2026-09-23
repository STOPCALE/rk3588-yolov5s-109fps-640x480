// =============================================================================
//  step5 故障注入练习（坏版本 #1）：提交帧 → 假推理 → 校验帧内容。
//  构建/运行方法见同目录 README.md（PC/板子均可，-pthread）。
//  现象与工作表：docs/teaching/step5-多线程流水线/fault-injection.md
// =============================================================================
#include <cstdio>
#include <cstdint>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <algorithm>
#include <chrono>

// 入队保存的"帧"：记录对像素缓冲的引用（以及本帧的编号）
struct Frame
{
    std::vector<uint8_t> *pixels;   // 笔者的理解：入队只是"排个队"，不必复制像素
    int seq;
};

class MiniPool
{
public:
    MiniPool() { worker = std::thread([this] { run(); }); }
    ~MiniPool()
    {
        { std::lock_guard<std::mutex> g(m); quit = true; }
        cv.notify_all();
        worker.join();
    }

    void submit(const Frame &f)
    {
        { std::lock_guard<std::mutex> g(m); q.push_back(f); }
        cv.notify_one();
    }

    int mismatches() const { return bad.load(); }

private:
    void run()
    {
        for (;;)
        {
            Frame f{};
            {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [this] { return quit || !q.empty(); });
                if (q.empty()) return;
                f = q.front();
                q.pop_front();
            }

            // 假推理：睡 30 ms（模拟一帧的真实推理耗时）
            std::this_thread::sleep_for(std::chrono::milliseconds(30));

            // 校验：像素首字节应当还是提交时的编号
            uint8_t got = (*f.pixels)[0];
            if (got != (uint8_t)(f.seq & 0xFF))
                bad.fetch_add(1);
        }
    }

    std::deque<Frame> q;
    std::mutex m;
    std::condition_variable cv;
    std::thread worker;
    bool quit = false;
    std::atomic<int> bad{0};
};

int main()
{
    const int N = 20;
    MiniPool pool;

    // "采集/解码复用同一块缓冲"（真实工程里 cv::Mat 也常被复用）
    std::vector<uint8_t> buf(320 * 240);

    for (int seq = 1; seq <= N; seq++)
    {
        std::fill(buf.begin(), buf.end(), 0);
        buf[0] = (uint8_t)(seq & 0xFF);

        Frame f{&buf, seq};
        pool.submit(f);

        // 主线程立刻复用缓冲准备"下一帧"（模拟采集线程的正常行为）
        std::fill(buf.begin(), buf.end(), 0);
        buf[0] = 0xAA;

        // 等这一帧被处理完（让节奏稳定）
        std::this_thread::sleep_for(std::chrono::milliseconds(35));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // drain
    printf("frames=%d  tag mismatch=%d\n", N, pool.mismatches());
    if (pool.mismatches() > 0)
        printf("!! every frame's data changed before the worker looked — who owns the pixels? !!\n");
    return 0;
}
