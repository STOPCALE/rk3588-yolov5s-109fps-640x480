// src/trajectory_predictor.cc —— 轨迹预测器实现（B9-3）
// ---------------------------------------------------------------------------
// 设计说明见头文件顶部。实现只做三件事：
//   ① 维护最近 hist_max 个"带真实时间戳"的检测点
//   ② 速度 = (最新点 - 最老点) / (两点的真实时间差)   [px/ms]
//   ③ 预测 = 基准点 + 速度 × (到"现在+L"的真实时间差)
//
// 与原工程的核心区别：原工程速度单位是"像素/帧"（假设帧间隔恒定），
// 一旦丢帧/时间戳不规则就偏；本实现全程用真实时间，天然免疫。
// ---------------------------------------------------------------------------

#include "trajectory_predictor.hpp"

void TrajectoryPredictor::reset()
{
    hist_.clear();
    lost_  = 0;
    has_v_ = false;
    vx_    = 0;
    vy_    = 0;
}

PredictOutput TrajectoryPredictor::update(const std::vector<vb_ball_t> &balls, double t_ms)
{
    PredictOutput out;

    // ---------- 1. 选目标 ----------
    //   无轨道（起步 / 刚复位）或关门限：取 prop 最高（原工程策略）
    //   有轨道且开门限：只认"预期位置 ± 门限"内、距离最近的球（目标锁定，B8-4）
    const vb_ball_t *best = nullptr;

    if (hist_.empty() || !gate_on)
    {
        for (const vb_ball_t &b : balls)
        {
            if (best == nullptr || b.prop > best->prop) best = &b;
        }
    }
    else
    {
        // 预期位置 = 最后一点沿速度外推到"本帧时刻"
        const Sample &last = hist_.back();
        float ex = last.cx;
        float ey = last.cy;
        if (has_v_)
        {
            ex += (float)(vx_ * (t_ms - last.t_ms));
            ey += (float)(vy_ * (t_ms - last.t_ms));
        }

        float best_d2 = (float)(gate_px * gate_px);     // 门限之内才考虑
        for (const vb_ball_t &b : balls)
        {
            const float dx = b.cx - ex;
            const float dy = b.cy - ey;
            const float d2 = dx * dx + dy * dy;
            if (d2 <= best_d2)
            {
                best_d2 = d2;
                best = &b;
            }
        }
        // 门限内没有候选 -> best 保持 nullptr -> 走"无检测"分支（丢帧外推）
    }

    if (best != nullptr)
    {
        // ---------- 2. 有检测：入历史 + 更新速度 ----------
        lost_ = 0;

        Sample s;
        s.cx = best->cx;  s.cy = best->cy;
        s.r  = best->radius;
        s.t_ms = t_ms;
        hist_.push_back(s);
        while ((int)hist_.size() > hist_max) hist_.pop_front();

        out.detect_ok = true;
        out.cx = best->cx;
        out.cy = best->cy;
        out.r  = best->radius;

        // 速度用最老/最新两点：有 3 个点时相当于跨 2 个区间，比"相邻两点"更稳
        if (hist_.size() >= 2)
        {
            const Sample &a = hist_.front();
            const Sample &z = hist_.back();
            const double  dt = z.t_ms - a.t_ms;     // 真实经过时间（ms）
            if (dt >= min_dt_ms)                    // 间隔过小的两点：噪声会淹没速度信号
            {                                       // （实拍：启动连发帧 Δt≈3ms → 77px 离群）
                vx_ = (float)((z.cx - a.cx) / dt);
                vy_ = (float)((z.cy - a.cy) / dt);
                has_v_ = true;
            }
        }

        // 预测 = 本帧检测点 + v × lead（"包到达接收端那一刻"的估计位置）
        if (has_v_)
        {
            out.predict_ok = true;
            out.pred_cx = (float)(out.cx + vx_ * lead_ms);
            out.pred_cy = (float)(out.cy + vy_ * lead_ms);
        }
        return out;
    }

    // ---------- 3. 无检测：丢帧处理 ----------
    if (hist_.empty()) return out;      // 从没检测到过（或已被重置）：无话可说

    lost_++;
    if (lost_ > max_lost)
    {
        reset();                        // 丢太久：清空状态，等下次检测重新起步
        return out;
    }

    // 还能预测：从"最后一次检测"沿轨迹外推到 (本帧时刻 + lead)
    if (has_v_)
    {
        const Sample &last = hist_.back();
        const double  dt   = (t_ms - last.t_ms) + lead_ms;
        out.predict_ok = true;
        out.pred_cx = (float)(last.cx + vx_ * dt);
        out.pred_cy = (float)(last.cy + vy_ * dt);
    }
    return out;
}
