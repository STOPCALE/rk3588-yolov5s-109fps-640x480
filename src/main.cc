#include <stdio.h>
#include "rkYolov5s.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"
#include <stdlib.h>
#include <string.h>
#include <opencv2/core/utility.hpp>
#include <opencv2/videoio.hpp>
#include <thread>
#include <atomic>
#include <deque>
#include <chrono>
#include <math.h>
#include "rknnPool.hpp" //模型池
#include "serial_proto.hpp"         //B9:15 字节协议包(组包+CRC)
#include "trajectory_predictor.hpp" //B9:轨迹预测器
#include "uart.h"                   //B9:串口底层

//计时系统，对实时系统进行判断的
struct StageStat
{
    double sum  = 0.0, mn = 0.0, mx = 0.0;
    int    n    = 0;

    void add(double v)
    {
        if (n == 0 || v < mn) mn = v;   //第一帧时初始化mn
        if (n == 0 || v > mx) mx = v;
        sum += v;
        ++n;
    }
    double avg() const { return n ? sum / n : 0.0; }
};

struct FrameSlot
{
    std::mutex mtx;
    cv::Mat    img;     //拥有像素
    int64_t    t    = 0;    //采集时刻
    uint64_t   seq  = 0;    //帧序号

    //采集线程，写入像素
    void write(cv::Mat m, int64_t tick)
    {
        std::lock_guard<std::mutex> lock(mtx);
        img = std::move(m);
        t   = tick;
        ++seq;
    }

    //结果线程：只更新比上次大的
    bool read_newer(uint64_t &last_seq, cv::Mat &out, int64_t &tick)
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (seq == last_seq) return false;
        last_seq    = seq;
        out         = std::move(img);
        tick        = t;
        return true;
    }

    //结果线程：收尾判断
    bool has_newer(uint64_t last_seq)
    {
        std::lock_guard<std::mutex> lock(mtx);
        return seq != last_seq;
    }
};

int main(int argc, char **argv)
{
    if (argc < 3 || argc > 22)
    {
        printf("Usage: %s <model_path> <image_path|video|/dev/videoN> [frames=3] [--quiet] [--fast] [--pipe] [--disp ms] [--serial dev] [--pred-log file] [--vis dir] [--nogate] [--cam-size WxH] [--cam-fps N] [--cam-yuyv] [--forever]\n", argv[0]);
        return -1;
    }

    //可选参数，第3-4位放循环次数或者quiet，fast顺序随意
    int max_frames  = 3;
    bool quiet      = false;
    bool fast       = false;    //全速喂帧
    bool pipe       = false;    //三线程流水线模式
    int disp_ms    = 500;      //显示间隔
    const char *serial_dev = nullptr;   //--serial <设备>:给了才发串口(B9)
    const char *pred_log_path = nullptr; //--pred-log <文件>:每帧检测轨迹 CSV(B9 评估用)
    const char *vis_path = nullptr;      //--vis <文件>:写带标注的结果视频(B9 可视化)
    bool nogate = false;                 //--nogate:关闭目标锁定(对比用)
    int  cam_w = 640, cam_h = 480;       //--cam-size WxH:摄像头分辨率(默认 VGA)
    int  cam_fps = 120;                  //--cam-fps N:摄像头帧率(默认 120)
    bool cam_yuyv = false;               //--cam-yuyv:用 YUYV 原始格式(默认 MJPG,高帧率必需)
    bool forever = false;                //--forever:无限模式(不数帧,跑到 Ctrl+C/被停)
    for (int i =3; i < argc; i++)
    {
        if      (strcmp(argv[i], "--quiet") == 0)    quiet = true;
        else if (strcmp(argv[i], "--fast")  == 0)    fast  = true;
        else if (strcmp(argv[i], "--pipe")  == 0)    pipe  = true;
        else if (strcmp(argv[i], "--disp")  == 0 && i + 1 <argc) disp_ms = atoi(argv[++i]);
        else if (strcmp(argv[i], "--serial")== 0 && i + 1 <argc) serial_dev = argv[++i];
        else if (strcmp(argv[i], "--pred-log")== 0 && i + 1 <argc) pred_log_path = argv[++i];
        else if (strcmp(argv[i], "--vis")    == 0 && i + 1 <argc) vis_path = argv[++i];
        else if (strcmp(argv[i], "--nogate") == 0)               nogate = true;
        else if (strcmp(argv[i], "--cam-size")== 0 && i + 1 <argc) { if (sscanf(argv[++i], "%dx%d", &cam_w, &cam_h) != 2) { cam_w = 640; cam_h = 480; } }
        else if (strcmp(argv[i], "--cam-fps")== 0 && i + 1 <argc) cam_fps = atoi(argv[++i]);
        else if (strcmp(argv[i], "--cam-yuyv")== 0)                cam_yuyv = true;
        else if (strcmp(argv[i], "--forever")== 0)                forever = true;
        else                                    max_frames = atoi(argv[i]);
    }
    if (max_frames <= 0) max_frames = 3;    //防呆
    printf("frame = %d quiet = %d fast = %d pipe = %d disp = %d serial = %s predlog = %s gate = %d vis = %s forever = %d\n",
           max_frames, (int)quiet, (int)fast, (int)pipe, disp_ms,
           serial_dev ? serial_dev : "(off)", pred_log_path ? pred_log_path : "(off)",
           (int)!nogate, vis_path ? vis_path : "(off)", (int)forever);     //回显



    rkYolov5s model(argv[1]);
    if (!pipe)  //流水线模式用的模型池
    {
        if (model.init(nullptr, false) != 0)
        {
            printf("model init failed\n");
            return -1;
        }

        //B8-1d:静音开关接到模型
        model.set_verbose(!quiet);
    }

    const double freq = cv::getTickFrequency();     //每秒多少tick
    StageStat st_read, st_infer;

    //判断第二个是图片/视频/摄像头(/dev/videoN)
    const bool is_cam = (strncmp(argv[2], "/dev/video", 10) == 0);   //B11:摄像头设备走 V4L2
    cv::Mat prode = is_cam ? cv::Mat() : cv::imread(argv[2]);

    cv::VideoCapture cap;   //播放器：有当前位置，能一帧一帧走
    bool is_video = false;

    if (prode.empty())
    {
        if (is_cam)
        {
            //B11 摄像头接入：V4L2。缓冲数实测定论：=1 会"每两帧丢一帧"(120fps→62fps)，
            //    =2/4/8 都能跑满 109.4fps —— 取 2（积压上限~2帧≈18ms，兼顾实时性）
            if (!cap.open(argv[2], cv::CAP_V4L2)) { printf("摄像头打不开: %s\n", argv[2]); return -1; }
            const int cam_buf = 2;      // 打印与实际使用同一变量，避免文案对不上
            cap.set(cv::CAP_PROP_BUFFERSIZE, cam_buf);
            if (!cam_yuyv) cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M','J','P','G'));
            cap.set(cv::CAP_PROP_FRAME_WIDTH,  cam_w);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, cam_h);
            cap.set(cv::CAP_PROP_FPS,          cam_fps);
            const int  fc = (int)cap.get(cv::CAP_PROP_FOURCC);
            printf("[cam] %s 实际: %dx%d %.1f fps '%c%c%c%c' (buffer=%d)\n",
                   argv[2], (int)cap.get(cv::CAP_PROP_FRAME_WIDTH), (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT),
                   cap.get(cv::CAP_PROP_FPS),
                   (char)(fc & 0xFF), (char)((fc >> 8) & 0xFF), (char)((fc >> 16) & 0xFF), (char)((fc >> 24) & 0xFF),
                   cam_buf);
        }
        else if (fast)
        {
            //自己拼完整pipeline
            char pipeline[1024];
            snprintf(pipeline, sizeof(pipeline),
                     "filesrc location=\"%s\" ! qtdemux ! h264parse ! avdec_h264 ! "
                     "videoconvert ! video/x-raw,format=BGR ! appsink sync=false",
                     argv[2]);
            if (!cap.open(pipeline, cv::CAP_GSTREAMER))
            {
                printf("--fast 的 pipeline 打不开：%s\n", pipeline);
                return -1;
            }
            printf("[fast] 已停用实时节流，全速喂帧\n");
        }
        //指定后端
        else if (!cap.open(argv[2], cv::CAP_GSTREAMER))
        {
            printf("[warn] GStreamer 打不开，退回默认后端（可能解不完整）\n");
            if (!cap.open(argv[2])) { printf("打不开: %s\n", argv[2]); return -1; }
        }
        is_video = true;

        printf("video: %dx%d %.1f fps %0.f 帧\n",
                (int)cap.get(cv::CAP_PROP_FRAME_WIDTH),
                (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT),
                cap.get(cv::CAP_PROP_FPS),
                cap.get(cv::CAP_PROP_FRAME_COUNT));
    }

    //三线程流水线
    if (pipe)
    {
        if (!is_video)
        {
            printf("--pipe 只支持视频/流出入\n");
            return -1;
        }

        //模型池：3实例/3NPU/共享权重
        rknnPool<rkYolov5s, cv::Mat, FrameResult> pool(argv[1], 3);
        if (pool.init() != 0) { printf("pool init failed\n"); return -1; }
        pool.setMaxPending(3);  //Little定律
        pool.setVerbose(!quiet);

        //B9:串口 + 轨迹预测器(给了 --serial 或 --pred-log 就启用)
        int uart_fd = -1;
        if (serial_dev)
        {
            uart_fd = uart_open(serial_dev, 115200);
            if (uart_fd < 0)    printf("[warn] 串口打不开:%s(继续运行,但不发包)\n", serial_dev);
            else                printf("[serial] %s @115200 已打开\n", serial_dev);
        }
        FILE *pred_log = nullptr;           //--pred-log:检测轨迹 CSV(离线评估用)
        if (pred_log_path)
        {
            pred_log = fopen(pred_log_path, "w");
            if (pred_log) { fprintf(pred_log, "t_ms,det_ok,cx,cy,r\n"); printf("[pred-log] %s\n", pred_log_path); }
            else            printf("[warn] 预测日志打不开:%s\n", pred_log_path);
        }
        TrajectoryPredictor predictor;      //轨迹预测器(真实时间戳版)
        if (nogate) predictor.gate_on = false;   //--nogate:关闭目标锁定(对比用)
        StageStat st_send;                  //串口发送耗时统计
        size_t pkt_try = 0, pkt_sent = 0, pkt_fail = 0;

        //B9可视化(--vis):带标注帧序列(由显示线程落盘,不拖慢主管线)
        std::deque<cv::Point> trail;        //尾迹(最近40个选中点)
        cv::Point start_pt(0, 0);           //本次跟踪起点
        bool has_start = false;
        bool prev_dok = false;

        //最新帧槽+采集线程
        FrameSlot slot;
        std::atomic<bool> running{true};
        std::atomic<bool> eos{false};
        size_t captured = 0;

        std::thread capThread([&]()
            {
                while (running)
                {
                    cv::Mat img;
                    if (!cap.read(img)) { eos = true; break; }
                    slot.write(std::move(img), cv::getTickCount());
                    ++captured;
                }
            }
        );

        //结果线程：喂帧，收结果，统计
        std::deque<int64_t> ts; //旁路时间帧
        StageStat st_lat;       //端到端延迟统计
        uint64_t last_seq = 0;
        size_t   results  = 0;
        const int64_t t_start = cv::getTickCount();

        //显示槽 + 显示线程
        FrameSlot dslot;
        std::atomic<bool> disp_run{true};
        size_t shown = 0;

        std::thread dispThread([&]()
            {
                uint64_t d_last = 0;
                cv::Mat  dimg;
                int64_t  dt     = 0;
                size_t   vsave  = 0;
                while (disp_run)
                {
                    if (dslot.read_newer(d_last, dimg, dt))
                    {
                        if (vis_path)
                        {
                            //--vis:全速落盘带标注帧(在显示线程做,主管线不受编码拖累)
                            char fn[600];
                            snprintf(fn, sizeof fn, "%s/f_%06u.jpg", vis_path, (unsigned)vsave);
                            cv::imwrite(fn, dimg);
                            ++vsave;
                        }
                        else
                        {
                            //可被打断的睡眠(模拟慢显示)
                            for (int left = disp_ms; left > 0 && disp_run; left -= 50)
                            {
                                std::this_thread::sleep_for(std::chrono::milliseconds(left < 50 ? left : 50));
                            }
                            cv::imwrite("/tmp/pipe_display.jpg", dimg); //模拟慢显示
                            ++shown;
                        }
                    }
                    else
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                }
                if (vis_path) printf("[vis] 已落盘 %u 帧 -> %s\n", (unsigned)vsave, vis_path);
            }
        );

        while (running)
        {
            bool did = false;

            //喂：有新帧就投池
            cv::Mat img;
            int64_t t_cap = 0;
            if (slot.read_newer(last_seq, img, t_cap))
            {
                if (pool.put(img) == 0)
                {
                    ts.push_back(t_cap);
                    did = true;
                }
            }

            //收：取最早结果

            FrameResult r;
            if (pool.get_try(r) == 0)
            {
                const int64_t t0 = ts.front(); ts.pop_front();
                st_lat.add((cv::getTickCount() - t0) * 1000.0 / freq);
                ++results;

                //长跑心跳：每 3000 帧（≈30s）打印一行——无限模式/后台日志的“体检指标”
                if (results % 3000 == 0)
                {
                    const double w_ms = (cv::getTickCount() - t_start) * 1000.0 / freq;
                    printf("[hb] %u 帧  墙钟 %.1f s  平均 %.1f fps  延迟 avg %.2f / max %.2f ms\n",
                           (unsigned)results, w_ms / 1000.0, results * 1000.0 / w_ms, st_lat.avg(), st_lat.mx);
                    fflush(stdout);
                }

                PredictOutput po{};                                 //本帧预测输出(组包/可视化共用)
                //B9:预测 -> 组包 -> 发串口(时间戳 = 本帧采集时刻)
                if (serial_dev || pred_log || vis_path)
                {
                    ++pkt_try;
                    const double t_ms = t0 * 1000.0 / freq;
                    po = predictor.update(r.balls, t_ms);

                    //偏差相对图像中心(沿用原工程约定)
                    const int icx = r.image.cols / 2;
                    const int icy = r.image.rows / 2;

                    int16_t  ddx = 0, ddy = 0, pdx = 0, pdy = 0;
                    uint16_t rad = 0;
                    if (po.detect_ok)
                    {
                        ddx = (int16_t)lround(po.cx - icx);
                        ddy = (int16_t)lround(po.cy - icy);
                        const float rr = po.r < 0 ? 0.f : (po.r > 65535.f ? 65535.f : po.r);
                        rad = (uint16_t)lround(rr);
                    }
                    if (po.predict_ok)
                    {
                        pdx = (int16_t)lround(po.pred_cx - icx);
                        pdy = (int16_t)lround(po.pred_cy - icy);
                    }

                    TargetPacket pkt;
                    proto_build(pkt, ddx, ddy, pdx, pdy, rad, po.detect_ok, po.predict_ok);

                    if (uart_fd >= 0)
                    {
                        const int64_t ts0 = cv::getTickCount();
                        const ssize_t wn = uart_write(uart_fd, &pkt, sizeof pkt);
                        st_send.add((cv::getTickCount() - ts0) * 1000.0 / freq);
                        if (wn == (ssize_t)sizeof pkt) ++pkt_sent;
                        else                           ++pkt_fail;
                    }

                    //每帧检测轨迹写 CSV(供 predictor_eval 离线评估)
                    if (pred_log)
                    {
                        fprintf(pred_log, "%.3f,%d,%.2f,%.2f,%.2f\n",
                                t_ms, (int)po.detect_ok, po.cx, po.cy, po.r);
                        fflush(pred_log);
                    }

                    //前 5 包打印 hex,方便肉眼对照
                    if (!quiet && pkt_try <= 5)
                    {
                        const uint8_t *pb = reinterpret_cast<const uint8_t *>(&pkt);
                        printf("[pkt %u] ", (unsigned)pkt_try);
                        for (size_t bi = 0; bi < sizeof pkt; bi++) printf("%02X ", pb[bi]);
                        printf("| det=%d pred=%d\n", (int)po.detect_ok, (int)po.predict_ok);
                    }
                }

                //画框
                for (int k = 0; k < (int)r.balls.size(); k++)
                {
                    const vb_ball_t &b = r.balls[k];
                    const cv::Point c((int)(b.cx + 0.5f), (int)(b.cy + 0.5f));
                    cv::circle(r.image, c, (int)(b.radius + 0.5f), cv::Scalar(0, 255, 0), 3);
                }

                //B9可视化(--vis):候选/锁定/尾迹/初始点/预测点/落点外推
                if (vis_path)
                {
                    // 全部候选(细灰)——包括被门限拒收的
                    for (int k = 0; k < (int)r.balls.size(); k++)
                    {
                        const vb_ball_t &b = r.balls[k];
                        const cv::Point c((int)(b.cx + 0.5f), (int)(b.cy + 0.5f));
                        cv::circle(r.image, c, (int)(b.radius + 0.5f), cv::Scalar(140, 140, 140), 2);
                    }

                    // 选中目标(绿圈红十字)
                    if (po.detect_ok)
                    {
                        const cv::Point c((int)(po.cx + 0.5f), (int)(po.cy + 0.5f));
                        cv::circle(r.image, c, (int)(po.r + 0.5f), cv::Scalar(0, 255, 0), 3);
                        cv::drawMarker(r.image, c, cv::Scalar(0, 0, 255), cv::MARKER_CROSS, 30, 2);
                    }

                    // 尾迹(选中点,最近40个) + 初始点标记
                    if (po.detect_ok)
                    {
                        if (!prev_dok) { start_pt = cv::Point((int)(po.cx + 0.5f), (int)(po.cy + 0.5f)); has_start = true; }
                        trail.push_back(cv::Point((int)(po.cx + 0.5f), (int)(po.cy + 0.5f)));
                        while (trail.size() > 40) trail.pop_front();
                    }
                    prev_dok = po.detect_ok;

                    for (size_t k = 1; k < trail.size(); k++)
                        cv::line(r.image, trail[k-1], trail[k], cv::Scalar(0, 180, 255), 2);

                    if (has_start)
                    {
                        cv::rectangle(r.image, cv::Rect(start_pt.x - 7, start_pt.y - 7, 14, 14), cv::Scalar(255, 128, 0), 2);
                        cv::putText(r.image, "START", start_pt + cv::Point(12, -12), cv::FONT_HERSHEY_SIMPLEX, 1.1, cv::Scalar(255, 128, 0), 3);
                    }

                    // 预测点(+25ms,青) 与 落点外推(0.1~0.5s,青点列)
                    if (po.predict_ok)
                    {
                        const cv::Point p25((int)(po.pred_cx + 0.5f), (int)(po.pred_cy + 0.5f));
                        cv::drawMarker(r.image, p25, cv::Scalar(255, 255, 0), cv::MARKER_TILTED_CROSS, 30, 3);

                        const float vx = (po.pred_cx - po.cx) / (float)predictor.lead_ms;
                        const float vy = (po.pred_cy - po.cy) / (float)predictor.lead_ms;
                        cv::Point pf((int)po.cx, (int)po.cy);
                        for (int ms = 100; ms <= 500; ms += 100)
                        {
                            const cv::Point pn((int)(po.cx + vx * ms), (int)(po.cy + vy * ms));
                            cv::line(r.image, pf, pn, cv::Scalar(255, 255, 0), 2);
                            pf = pn;
                        }
                        cv::circle(r.image, pf, 10, cv::Scalar(255, 255, 0), 2);
                        cv::putText(r.image, "LAND(est)", pf + cv::Point(14, -14), cv::FONT_HERSHEY_SIMPLEX, 1.1, cv::Scalar(255, 255, 0), 3);
                    }

                    // 图像中心(偏差基准)
                    cv::drawMarker(r.image, cv::Point(r.image.cols / 2, r.image.rows / 2), cv::Scalar(255, 255, 255), cv::MARKER_CROSS, 60, 2);

                    // 状态文本
                    char vis_txt[256];
                    snprintf(vis_txt, sizeof vis_txt, "f=%u  det=%d pred=%d  cand=%d  r=%.0f",
                             (unsigned)results, (int)po.detect_ok, (int)po.predict_ok, (int)r.balls.size(), po.r);
                    cv::putText(r.image, vis_txt, cv::Point(24, 64), cv::FONT_HERSHEY_SIMPLEX, 1.3, cv::Scalar(0, 255, 0), 3);
                }

                //把结果帧交给显示线程
                dslot.write(std::move(r.image), 0);

                did = true;
                if (!forever && (int)results >= max_frames) break;
            }

            //收尾：采集结束+池空+没新帧+全处理完了
            if (eos && pool.pending() == 0 && !slot.has_newer(last_seq)) break;

            //没事干睡1ms
            if (!did)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        running     = false;
        disp_run    = false;
        capThread.join();
        dispThread.join();

        //汇总
        const double wall = (cv::getTickCount() - t_start) * 1000.0 / freq;
        printf("\n=========== 流水线汇总（--pipe）===========\n");
        printf("  采集 %u 帧   结果 %u 帧   墙钟 %.2f s\n",
               (unsigned)captured, (unsigned)results, wall / 1000.0);
        printf("  结果吞吐 %.1f fps   采集吞吐 %.1f fps\n",
               results * 1000.0 / wall, captured * 1000.0 / wall);
        printf("  端到端延迟 ms: 平均 %.2f  最小 %.2f  最大 %.2f\n", st_lat.avg(), st_lat.mn, st_lat.mx);
        printf("  拒收 %u 帧   残余在途 %u\n", (unsigned)pool.dropped(), (unsigned)pool.pending());
        printf("  显示存图 %u 张(间隔 %d ms)\n", (unsigned)shown, disp_ms);
        if (serial_dev)
        {
            printf("  串口发包 %u 个(失败 %u) 发送耗时 ms: 平均 %.3f 最大 %.3f\n",
                   (unsigned)pkt_sent, (unsigned)pkt_fail, st_send.avg(), st_send.mx);
        }
        if (vis_path) printf("  可视化帧目录: %s（用 ffmpeg 合成视频）\n", vis_path);
        if (uart_fd >= 0) uart_close(uart_fd);
        if (pred_log)     fclose(pred_log);
        return 0;
    }

    for (int f = 0; f < max_frames; f++)
    {
        if (!quiet) printf("--- frame %d ---\n", f);

        //取一帧
        int64_t t = cv:: getTickCount();
        cv::Mat img;

        if (is_video)
        {
            //读下一帧，读到结尾返回false
            if (!cap.read(img)) { printf("视频结束（共%d帧）\n", f); break; }
        }
        else
        {
            //图片：从头解读一边
            img = cv::imread(argv[2]);
            if (img.empty())    { printf("读不到图片：%s\n", argv[2]); return -1; }
        }

        st_read.add((cv::getTickCount() - t) * 1000.0 / freq);

        if (f == 0 && !is_video)    { printf("image: %dx%d channels=%d\n", img.cols, img.rows, img.channels()); }


        //推理一帧
        t = cv::getTickCount();
        FrameResult r =model.infer(img);
        st_infer.add((cv::getTickCount() - t) * 1000.0 / freq);

        if (!quiet) printf("检测到 %d 个球;\n", (int)r.balls.size());
        for (int k = 0; k < (int)r.balls.size(); k++)
        {
            const vb_ball_t &b = r.balls[k];
            if (!quiet) printf(" [%d] cx=%.1f cy=%.1f r=%.1f prop=%.3f\n",
                                k, b.cx, b.cy, b.radius, b.prop);

            //画圆(绿)
            const cv::Point c((int)(b.cx + 0.5f), (int)(b.cy + 0.5f));
            cv::circle(img, c, (int)(b.radius + 0.5f), cv::Scalar(0, 255, 0), 3);
            //画出十字中心(红)
            cv::drawMarker(img, c, cv::Scalar(0, 0, 255), cv::MARKER_CROSS, 24, 2);
        }

        if (f == 0) //只存第一帧
        {
            const char *out = "/tmp/b7_result.jpg";
            if (cv::imwrite(out, img))  printf("已保存：%s\n", out);
            else                        printf("保存失败：%s\n", out);
        }
    }

    printf("\n=========== 汇总（每帧耗时 ms）===========\n");
    printf("  阶段        平均       最小       最大\n");
    printf("  read    %8.2f   %8.2f   %8.2f\n", st_read.avg(),  st_read.mn,  st_read.mx);
    printf("  infer   %8.2f   %8.2f   %8.2f\n", st_infer.avg(), st_infer.mn, st_infer.mx);
    printf("B7-3 OK\n");
    return 0;

}
