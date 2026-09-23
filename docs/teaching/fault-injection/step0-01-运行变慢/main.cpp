// =============================================================================
//  step0 故障注入练习（坏版本 #1）：一个"热点循环"基准程序。
//  构建/运行方法见同目录 README.md。
//  现象与工作表：docs/teaching/step0-cpp-骨架/fault-injection.md
// =============================================================================
#include <chrono>
#include <cstdio>

static double hot_sum(int n)
{
    double sum = 0.0;
    for (int i = 1; i <= n; ++i)
    {
        double x = (double)i;
        sum += x / (x + 1.0);
    }
    return sum;
}

int main()
{
    const int n = 200000000;   // 2×10^8 次迭代

    auto t0 = std::chrono::steady_clock::now();
    double s = hot_sum(n);
    auto t1 = std::chrono::steady_clock::now();

    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("sum=%.6f  elapsed=%.1f ms  (n=%d)\n", s, ms, n);
    return 0;
}
