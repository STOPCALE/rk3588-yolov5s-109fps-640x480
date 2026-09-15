#ifndef TRAJECTORY_PREDICTOR_HPP
#define TRAJECTORY_PREDICTOR_HPP

#include <deque>
#include "opencv2/core/core.hpp"
#include "postprocess.h"

class TrajectoryPredictor {
public:
    TrajectoryPredictor();
    void update(const detect_result_group_t& detections);
    void draw(cv::Mat& img, int future_frames) const;

    bool has_prediction() const;
    cv::Point get_predicted_center(int future_frames) const;
    cv::Rect  get_predicted_box(int future_frames) const;

    cv::Point get_current_center() const;
    uint16_t get_current_area() const;   // 新增：当前检测目标面积（像素）

private:
    struct TrackPoint {
        float cx, cy, w, h;
    };

    bool prediction_enabled(const TrackPoint& p) const;
    bool select_target(const detect_result_group_t& detections, TrackPoint& target) const;

    std::deque<TrackPoint> history_;
    TrackPoint current_;
    uint16_t current_area_;    // 新增：缓存当前面积
    bool has_current_;
    int lost_frames_;
};

#endif