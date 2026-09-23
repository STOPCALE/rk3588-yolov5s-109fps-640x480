// =============================================================================
//  step5 故障注入练习（坏版本 #2）：3 worker 假模型池 + 两种"喂/收"节奏对照。
//  构建/运行方法见同目录 README.md（PC/板子均可，-pthread）。
//  现象与工作表：docs/teaching/step5-多线程流水线/fault-injection.md
// =============================================================================
#include <cstdio>
#include <deque>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

static const int WORKERS = 3;
static const int INFER_MS = 24;   // 假推理耗时（真实工程单帧 run ≈22~24ms）

class Pool
{
public:
    Pool()
    {
        for (int i = 0; i < WORKERS; i++)
            ws.emplace_back([this] { run(); });
    }
    ~Pool()
    {
        { std::lock_guard<std::mutex> g(tm); quit = true; }
        cv_task.notify_all();
        for (auto &t : ws) t.join();
    }

    // 提交任务（不返回未来对象；结果统一用 get() 按完成顺序取）
    void submit(int seq)
    {
        { std::lock_guard<std::mutex> g(tm); tq.push_back(seq); }
        cv_task.notify_one();
    }

    // 取一个"已完成任务"的结果（没有完成的任务就等待）
    int get()
    {
        std::unique_lock<std::mutex> lk(rm);
        cv_res.wait(lk, [this] { return !rq.empty(); });
        int r = rq.front();
        rq.pop_front();
        return r;
    }

private:
    void run()
    {
        for (;;)
        {
            int seq;
            {
                std::unique_lock<std::mutex> lk(tm);
                cv_task.wait(lk, [this] { return quit || !tq.empty(); });
                if (tq.empty()) return;
                seq = tq.front();
                tq.pop_front();
            }

            // 假推理
            std::this_thread::sleep_for(std::chrono::milliseconds(INFER_MS));

            { std::lock_guard<std::mutex> g(rm); rq.push_back(seq * 2); }
            cv_res.notify_one();
        }
    }

    std::deque<int> tq;                 // 待执行任务（worker 侧）
    std::mutex tm;
    std::condition_variable cv_task;

    std::deque<int> rq;                 // 已完成结果（调用方侧）
    std::mutex rm;
    std::condition_variable cv_res;

    std::vector<std::thread> ws;
    bool quit = false;
};

int main()
{
    const int N = 400;
    using clk = std::chrono::steady_clock;

    // ---- 阶段 A：提交一帧 → 立刻等结果 ----
    {
        Pool pool;
        auto t0 = clk::now();
        for (int i = 0; i < N; i++)
        {
            pool.submit(i);
            volatile int r = pool.get();   // 等这一帧出结果
            (void)r;
        }
        auto t1 = clk::now();
        double s = std::chrono::duration<double>(t1 - t0).count();
        printf("[A] submit-then-wait : %d tasks in %.2f s -> %.1f fps\n", N, s, N / s);
    }

    // ---- 阶段 B：先把任务都喂出去，最后统一收结果 ----
    {
        Pool pool;
        auto t0 = clk::now();
        for (int i = 0; i < N; i++)
            pool.submit(i);
        for (int i = 0; i < N; i++)
        {
            volatile int r = pool.get();
            (void)r;
        }
        auto t1 = clk::now();
        double s = std::chrono::duration<double>(t1 - t0).count();
        printf("[B] submit-all       : %d tasks in %.2f s -> %.1f fps\n", N, s, N / s);
    }

    printf("worker count = %d, fake infer = %d ms\n", WORKERS, INFER_MS);
    return 0;
}
