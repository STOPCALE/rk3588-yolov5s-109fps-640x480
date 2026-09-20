// src/predictor_eval.cc —— 预测数据评估工具（验证工具，不进 CMake）
// ---------------------------------------------------------------------------
// 两种模式：
//   predictor_eval                 -> 合成轨迹模式：造一条"飞行中的球"轨迹(含丢帧)，
//                                     经真实预测器回放，打印 检测/预测/真值/误差 表 + 统计
//   predictor_eval <csv> [lead_ms] -> 实拍回放模式：读 --pred-log 产出的检测轨迹 CSV，
//                                     回放 + 与"未来真值"比对，打印同样的表和统计
// CSV 格式（--pred-log 输出）：t_ms,det_ok,cx,cy,r （每帧一行，毫秒时间戳）
//
// 评估口径（"预测到底准不准"）：
//   真值@t+lead = 轨迹中 (t+lead) 时刻的真实位置（相邻两帧线性插值）
//   预测误差    = |预测(t) − 真值(t+lead)|     ← 预测器的误差
//   基线误差    = |最近一次检测 − 真值(t+lead)|   ← 零阶保持（冻结在最近一次检测）
//   （预测器的意义：误差应远小于基线；提升倍数 = 基线/预测）
//
// 编译（纯 CPU 无依赖；PC / 板子均可）：
//   g++ -Wall -Wextra -std=c++14 -Iinclude src/predictor_eval.cc src/trajectory_predictor.cc -o /tmp/predictor_eval
// ---------------------------------------------------------------------------

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "trajectory_predictor.hpp"

struct Row { double t; int ok; float x, y, r; };                  // 检测轨迹一行
struct Rec { double t; int dok, pok, bl; float x, y, r, px, py, bx, by; };  // 回放结果(bl=有基线参考)

// ---------- 回放：把检测轨迹喂给真实预测器 ----------
static std::vector<Rec> replay(const std::vector<Row> &trace, float lead_ms)
{
    TrajectoryPredictor pred;
    pred.lead_ms = lead_ms;

    std::vector<Rec> out;
    out.reserve(trace.size());

    float lx = 0, ly = 0;      // 最近一次检测位置(→ 零阶保持基线)
    bool  has_l = false;

    for (size_t i = 0; i < trace.size(); i++)
    {
        const Row &w = trace[i];

        std::vector<vb_ball_t> balls;
        if (w.ok)
        {
            vb_ball_t b;
            b.cx = w.x; b.cy = w.y; b.radius = w.r; b.prop = 0.9f;
            balls.push_back(b);
            lx = w.x; ly = w.y; has_l = true;
        }

        PredictOutput o = pred.update(balls, w.t);

        Rec r;
        r.t   = w.t;
        r.dok = o.detect_ok ? 1 : 0;
        r.pok = o.predict_ok ? 1 : 0;
        r.bl  = has_l ? 1 : 0;
        r.x = o.cx;  r.y = o.cy;  r.r = o.r;
        r.px = o.pred_cx;  r.py = o.pred_cy;
        r.bx = lx;  r.by = ly;
        out.push_back(r);
    }
    return out;
}

// ---------- 求 t 时刻的"真值"：找相邻两帧插值 ----------
static bool truth_at(const std::vector<Rec> &recs, double t, float &tx, float &ty)
{
    for (size_t j = 0; j + 1 < recs.size(); j++)
    {
        if (recs[j].dok && recs[j + 1].dok &&
            recs[j].t <= t && t <= recs[j + 1].t)
        {
            const double w = (t - recs[j].t) / (recs[j + 1].t - recs[j].t);
            tx = (float)(recs[j].x + (recs[j + 1].x - recs[j].x) * w);
            ty = (float)(recs[j].y + (recs[j + 1].y - recs[j].y) * w);
            return true;
        }
    }
    return false;
}

// ---------- 中位数（数据量小，插入排序即可） ----------
static double median(std::vector<double> v)
{
    if (v.empty()) return 0;
    for (size_t i = 1; i < v.size(); i++)
        for (size_t j = i; j > 0 && v[j] < v[j - 1]; j--)
        {
            const double q = v[j]; v[j] = v[j - 1]; v[j - 1] = q;
        }
    return v[v.size() / 2];
}

int main(int argc, char **argv)
{
    float lead_ms = 25.0f;
    std::vector<Row> trace;

    if (argc >= 2)
    {
        // ---- 实拍回放模式：读 CSV ----
        FILE *f = fopen(argv[1], "r");
        if (!f) { printf("打不开: %s\n", argv[1]); return 1; }

        char line[256];
        bool first = true;
        while (fgets(line, sizeof line, f))
        {
            if (first) { first = false; if (strstr(line, "t_ms")) continue; }   // 跳过表头
            Row w;
            if (sscanf(line, "%lf,%d,%f,%f,%f", &w.t, &w.ok, &w.x, &w.y, &w.r) == 5)
                trace.push_back(w);
        }
        fclose(f);

        if (argc >= 3) lead_ms = (float)atof(argv[2]);
        printf("[eval] 实拍回放: %s   帧数=%u   lead=%.1f ms\n", argv[1], (unsigned)trace.size(), lead_ms);
    }
    else
    {
        // ---- 合成模式：模拟"飞行中的球"在画面里移动 ----
        //   x(t) = 150 + 0.45 t          （t 单位 ms，速度 ≈ 0.45 px/ms）
        //   y(t) = 300 - 0.9 t + 0.0025 t^2   （带加速度，像抛体）
        //   每 10ms 一帧共 40 帧；第 12~14 帧、第 25 帧人为丢检测
        for (int i = 0; i < 40; i++)
        {
            const double t = 10.0 * i;
            Row w;
            w.t  = t;
            w.ok = !(i >= 12 && i <= 14) && (i != 25);
            w.x  = (float)(150.0 + 0.45 * t);
            w.y  = (float)(300.0 - 0.9 * t + 0.0025 * t * t);
            w.r  = 20;
            trace.push_back(w);
        }
        printf("[eval] 合成轨迹: 40 帧 / 每 10ms / 含 4 帧丢检测   lead=%.1f ms\n", lead_ms);
    }

    // ---- 回放 + 评估 ----
    std::vector<Rec> recs = replay(trace, lead_ms);

    std::vector<double> errs, bases;
    int n_truth = 0, hit5 = 0, hit10 = 0;

    printf("\n  i   t(ms)   检测(x,y)          预测(x,y)          真值@+%.0fms        预测误差  基线误差\n", lead_ms);
    printf(" ---  ------  -----------------  -----------------  -----------------  --------  --------\n");

    for (size_t i = 0; i < recs.size(); i++)
    {
        const Rec &r = recs[i];

        float tx = 0, ty = 0;
        const bool has_truth = r.pok && truth_at(recs, r.t + lead_ms, tx, ty);

        if (!r.dok) printf(" %3u  %6.0f  %-17s", (unsigned)i, r.t, "(丢检测)");
        else        printf(" %3u  %6.0f  (%6.1f,%6.1f) ", (unsigned)i, r.t, r.x, r.y);

        if (!r.pok) printf(" %-17s", "(无预测)");
        else        printf(" (%6.1f,%6.1f) ", r.px, r.py);

        if (has_truth)
        {
            const double e = hypot(r.px - tx, r.py - ty);
            errs.push_back(e);
            ++n_truth;
            if (e < 5)  ++hit5;
            if (e < 10) ++hit10;

            if (r.bl)
            {
                const double b = hypot(r.bx - tx, r.by - ty);
                bases.push_back(b);
                printf(" (%6.1f,%6.1f)  %8.2f  %8.2f\n", tx, ty, e, b);
            }
            else
            {
                printf(" (%6.1f,%6.1f)  %8.2f  %8s\n", tx, ty, e, "-");
            }
        }
        else
        {
            printf(" %-17s       -         -\n", "—");
        }
    }

    // ---- 统计 ----
    if (n_truth > 0)
    {
        double se = 0, sb = 0, mx = 0;
        for (size_t i = 0; i < errs.size(); i++) { se += errs[i]; if (errs[i] > mx) mx = errs[i]; }
        for (size_t i = 0; i < bases.size(); i++) sb += bases[i];

        const double me = se / errs.size();
        const double mb = bases.empty() ? 0 : sb / bases.size();
        printf("\n=========== 统计（%d 帧可比对）===========\n", n_truth);
        printf("  预测误差 px: 平均 %6.2f   中位 %6.2f   最大 %6.2f\n", me, median(errs), mx);
        printf("  基线误差 px: 平均 %6.2f   中位 %6.2f   （零阶保持 = 冻结在最近一次检测）\n",
               mb, median(bases));
        if (!bases.empty())
            printf("  提升: 平均误差 %.1f 倍   准确率: <5px %.0f%%   <10px %.0f%%\n",
                   mb / me, 100.0 * hit5 / n_truth, 100.0 * hit10 / n_truth);
    }
    else
    {
        printf("\n（没有可比对的帧——数据里预测或真值不足）\n");
    }
    return 0;
}
