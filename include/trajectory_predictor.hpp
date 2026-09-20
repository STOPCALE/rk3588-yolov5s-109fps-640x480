#ifndef TRAJECTORY_PREDICTOR_HPP
#define TRAJECTORY_PREDICTOR_HPP

// ===========================================================================
// 轨迹预测器（B9-3）—— 用"真实时间戳"代替原工程的"等帧间隔"假设
// ---------------------------------------------------------------------------
// 背景：球是抛体运动，而相机每帧的时间间隔是**不规则**的（丢帧、拒收都会发生）。
// 所以速度必须用 (位移 / 真实经过时间) 计算 —— 数"帧数"在丢帧时会错。
//
// 工作方式（每帧调用一次 update）：
//   · 有检测 -> 选 prop 最高的球，存入历史（最多 hist_max 个点）；
//               用"最老/最新"两点算速度 v（px/ms）
//   · 无检测 -> 丢帧数没超限时，用"最后一次检测 + 缓存速度"继续外推
//   预测输出 = 基准点 + v × (到"现在 + lead_ms"的真实时间差)
//            （lead_ms = 补偿端到端延迟的前瞻，B8-3 实测 ≈25ms）
//
// 所有可调参数集中在下面"配置区"，改参数只动这几行。
// ===========================================================================

#include <deque>
#include <vector>

#include "postprocess.h"   // vb_ball_t

// 一帧的预测器输出（主程序拿去组 15B 包）
struct PredictOutput
{
    bool  detect_ok  = false;        // 本帧是否检测到（选中）目标
    float cx = 0, cy = 0;            // 检测中心（detect_ok 时有效）
    float r  = 0;                    // 检测半径

    bool  predict_ok = false;        // 预测是否有效
    float pred_cx = 0, pred_cy = 0;  // 延迟补偿后的预计位置
};

class TrajectoryPredictor
{
public:
    // ------- 配置区（要调参只动这里） -------
    double lead_ms  = 25.0;   // 延迟补偿前瞻（ms）：预测"包到达接收端那一刻"球在哪
    int    hist_max = 3;      // 历史点数（2~3 点估速度；越多越平滑、越迟钝）
    int    max_lost = 5;      // 连续丢帧上限：超过它预测作废，等下次检测重新起步
    // ---------------------------------------

    TrajectoryPredictor() = default;

    // 每帧调用：传入本帧全部球 + 本帧的采集时刻（ms，单调递增）
    PredictOutput update(const std::vector<vb_ball_t> &balls, double t_ms);

    // 清空状态（丢失太久会自动调用；也可以手动调用重新开始跟踪）
    void reset();

private:
    struct Sample { float cx, cy, r; double t_ms; };

    std::deque<Sample> hist_;    // 最近 hist_max 个"有效检测"点（带时间戳）
    int   lost_  = 0;            // 连续未检测帧数
    bool  has_v_ = false;        // 是否已算出速度
    float vx_ = 0, vy_ = 0;      // 速度（px/ms）
};

#endif // TRAJECTORY_PREDICTOR_HPP
