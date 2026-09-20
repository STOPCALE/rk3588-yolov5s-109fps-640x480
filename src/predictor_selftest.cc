// src/predictor_selftest.cc —— 轨迹预测器自测（验证工具，不进 CMake）
// ---------------------------------------------------------------------------
// 测什么：用"合成数据"构造能心算的场景，验证预测器的数学与状态机：
//   T1 匀速直线 + 均匀时间戳  -> 速度、预测值逐项手算对照
//   T2 不规则时间戳（模拟丢帧）-> 证明"真实时间戳"修复了原工程"按帧"算法的偏差
//   T3 丢帧期间继续外推 + 超过容限自动复位
//   T4 多球选择（取 prop 最高）
//   T5 同时间戳防除零（不崩溃、不产生无效速度）
// 判据：全部 PASS 打印 "全部 PASS"（退出码 0）
//
// 编译（纯 CPU 无依赖，PC / 板子均可）：
//   g++ -Wall -Wextra -std=c++14 -Iinclude src/predictor_selftest.cc src/trajectory_predictor.cc -o /tmp/predictor_selftest
// 运行： /tmp/predictor_selftest
// ---------------------------------------------------------------------------

#include <math.h>
#include <stdio.h>

#include "trajectory_predictor.hpp"

static int fails = 0;

static void check(bool ok, const char *name)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) fails++;
}

static bool near(float a, float b) { return fabsf(a - b) < 1e-3f; }

static std::vector<vb_ball_t> one(float cx, float cy, float r = 10, float prop = 0.9f)
{
    vb_ball_t b;
    b.cx = cx; b.cy = cy; b.radius = r; b.prop = prop;
    return std::vector<vb_ball_t>{ b };
}

static const std::vector<vb_ball_t> none;   // 空：本帧没检测到

int main()
{
    TrajectoryPredictor pred;

    // ================= T1 匀速直线 + 均匀时间戳 =================
    // 采样：(100,200) t=0 -> (110,195) t=10 -> (120,190) t=20
    // 手算：vx=(110-100)/10=1.0 px/ms ; vy=(195-200)/10=-0.5 px/ms
    // 预测（第2帧起）：(110+1*25, 195-0.5*25) = (135, 182.5)
    //                  第3帧： (120+1*25, 190-0.5*25) = (145, 177.5)
    printf("[T1] 匀速直线（均匀时间戳）\n");
    PredictOutput o1 = pred.update(one(100, 200), 0);
    check(o1.detect_ok && !o1.predict_ok, "第1帧：有检测、无速度（1 个点）");

    PredictOutput o2 = pred.update(one(110, 195), 10);
    printf("      第2帧 pred=(%.3f, %.3f)  期望 (135, 182.5)\n", o2.pred_cx, o2.pred_cy);
    check(o2.predict_ok && near(o2.pred_cx, 135.0f) && near(o2.pred_cy, 182.5f),
          "第2帧预测 = 手算 (135, 182.5)");

    PredictOutput o3 = pred.update(one(120, 190), 20);
    printf("      第3帧 pred=(%.3f, %.3f)  期望 (145, 177.5)\n", o3.pred_cx, o3.pred_cy);
    check(o3.predict_ok && near(o3.pred_cx, 145.0f) && near(o3.pred_cy, 177.5f),
          "第3帧预测 = 手算 (145, 177.5)");

    // ================= T2 不规则时间戳（丢帧不偏） =================
    // 采样：(0,0) t=0 -> (20,0) t=10 -> (30,0) t=15
    // 真实速度恒为 2 px/ms（每 5ms 走 10px，第二帧之后丢过帧/间隔不均）
    // 本实现：v=(30-0)/(15-0)=2 px/ms -> pred=(30+2*25, 0)=(80, 0)
    // 原工程"按帧"公式：v=(p3-p1)/2=15 px/帧，若外推 2 帧 -> 30+30=60（偏 25%）
    printf("[T2] 不规则时间戳（模拟丢帧）\n");
    pred.reset();
    pred.update(one(0, 0), 0);
    pred.update(one(20, 0), 10);
    PredictOutput o4 = pred.update(one(30, 0), 15);
    printf("      本实现 pred=(%.3f, %.3f)  期望 (80, 0)\n", o4.pred_cx, o4.pred_cy);
    printf("      （对照：原工程\"按帧\"公式在同场景给 60.0，偏 25%%）\n");
    check(o4.predict_ok && near(o4.pred_cx, 80.0f) && near(o4.pred_cy, 0.0f),
          "不规则时间戳下预测 = 手算 (80, 0)");

    // ================= T3 丢帧续推 + 超限复位 =================
    // 采样：(100,200) t=0 -> (110,195) t=10，之后连续丢帧
    // 手算：v=(1, -0.5) px/ms；丢帧第 k 帧（时刻 t）从最后检测外推：
    //   dt = (t-10) + 25
    //   t=20 -> dt=35 -> (145, 177.5)     t=60 -> dt=75 -> (185, 157.5)（第5次丢帧）
    //   t=70 -> 第6次丢帧 > max_lost(5) -> 作废，predict_ok=0
    printf("[T3] 丢帧续推 + 超限复位\n");
    pred.reset();
    pred.update(one(100, 200), 0);
    pred.update(one(110, 195), 10);

    PredictOutput o5 = pred.update(none, 20);
    printf("      丢帧1 @t=20 pred=(%.3f, %.3f)  期望 (145, 177.5)\n", o5.pred_cx, o5.pred_cy);
    check(!o5.detect_ok && o5.predict_ok && near(o5.pred_cx, 145.0f) && near(o5.pred_cy, 177.5f),
          "丢帧1：仍能外推 (145, 177.5)");

    pred.update(none, 30);
    pred.update(none, 40);
    pred.update(none, 50);
    PredictOutput o6 = pred.update(none, 60);
    printf("      丢帧5 @t=60 pred=(%.3f, %.3f)  期望 (185, 157.5)\n", o6.pred_cx, o6.pred_cy);
    check(o6.predict_ok && near(o6.pred_cx, 185.0f) && near(o6.pred_cy, 157.5f),
          "丢帧5：还在容限内，继续外推 (185, 157.5)");

    PredictOutput o7 = pred.update(none, 70);
    check(!o7.detect_ok && !o7.predict_ok, "丢帧6：超过容限 -> 作废（predict_ok=0）");

    PredictOutput o8 = pred.update(one(200, 150), 80);
    check(o8.detect_ok && !o8.predict_ok, "复位后重新起步：有检测、1 个点无预测");

    // ================= T4 多球选择（取 prop 最高） =================
    printf("[T4] 多球选择\n");
    pred.reset();
    std::vector<vb_ball_t> two;
    {   vb_ball_t a; a.cx = 1; a.cy = 1; a.radius = 5; a.prop = 0.50f; two.push_back(a);
        vb_ball_t b; b.cx = 2; b.cy = 2; b.radius = 6; b.prop = 0.80f; two.push_back(b); }
    PredictOutput o9 = pred.update(two, 0);
    check(o9.detect_ok && near(o9.cx, 2.0f) && near(o9.r, 6.0f), "选中 prop 更高的球 (cx=2, r=6)");

    // ================= T5 同一时间戳防除零 =================
    printf("[T5] 同一时间戳（防除零）\n");
    pred.reset();
    pred.update(one(0, 0), 0);
    PredictOutput o10 = pred.update(one(10, 10), 0);   // 时间没走
    check(o10.detect_ok && !o10.predict_ok, "两个点同一时间戳：不产生速度（predict_ok=0）");

    // ================= T6 目标锁定/门限（B8-4） =================
    // 1) 锁定小球 A 后，同帧出现"远处高分球 B"：应续跟 A（近处的），不追 B（高分的）
    // 2) A 消失、只剩门限外的 B：拒收（丢帧）
    // 3) 丢超限复位后：锚点（A 的位置）保护——远处 B 不锁；锚点半径内的 C 才锁
    // 4) 锚点超时后：允许全局重锁
    printf("[T6] 目标锁定/门限（B8-4）\n");
    pred.reset();
    pred.relock_px         = 250.0;
    pred.relock_timeout_ms = 5000.0;
    pred.update(one(100, 100, 10, 0.5f), 0);
    pred.update(one(100, 100, 10, 0.5f), 33);
    {
        std::vector<vb_ball_t> two;
        {   vb_ball_t a; a.cx = 105; a.cy = 100; a.radius = 10; a.prop = 0.50f; two.push_back(a);
            vb_ball_t b; b.cx = 500; b.cy = 100; b.radius = 12; b.prop = 0.95f; two.push_back(b); }
        PredictOutput o = pred.update(two, 66);
        check(o.detect_ok && near(o.cx, 105.0f), "同帧 A(105) 与 B(500)：续跟 A，不追高分 B");
    }
    PredictOutput o11 = pred.update(one(500, 100, 12, 0.95f), 99);
    check(!o11.detect_ok, "只剩门限外的 B：拒收（按丢帧处理）");
    pred.update(none, 132);
    pred.update(none, 165);
    pred.update(none, 198);
    pred.update(none, 231);
    pred.update(none, 264);                        // 第 6 帧触发复位（保留锚点 = A 的位置）
    PredictOutput o12 = pred.update(one(500, 100, 12, 0.95f), 297);
    check(!o12.detect_ok, "复位后：远处 B(500) 在锚点半径外，不锁");
    PredictOutput o13 = pred.update(one(300, 100, 12, 0.80f), 330);
    check(o13.detect_ok && near(o13.cx, 300.0f), "锚点(100) 半径内的 C(300)：重新锁定");
    // 锚点超时 -> 全局重锁
    pred.reset();
    pred.relock_timeout_ms = 150.0;
    pred.update(one(100, 100, 10, 0.5f), 0);
    pred.update(one(100, 100, 10, 0.5f), 33);
    pred.update(none, 66);  pred.update(none, 99);  pred.update(none, 132);
    pred.update(none, 165); pred.update(none, 198); pred.update(none, 231);   // 复位
    PredictOutput o14 = pred.update(one(500, 100, 12, 0.95f), 300);
    check(o14.detect_ok && near(o14.cx, 500.0f), "锚点超时(>150ms)后：全局重新锁定 B(500)");

    // ---- 汇总 ----
    if (fails == 0) printf("\n全部 PASS\n");
    else            printf("\n有 %d 项 FAIL\n", fails);
    return fails == 0 ? 0 : 1;
}
