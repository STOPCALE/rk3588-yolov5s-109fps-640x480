#include <stdio.h>
#include "rkYolov5s.hpp"
#include  "rknnPool.hpp"
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
#include <unistd.h>
#include <opencv2/highgui.hpp>

//计时系统
struct StageStat
{
    double sum = 0.0, mn = 0.0, mx = 0.0;
    int    n   = 0;

    void add(double v)
    {
        if (n == 0 || v < mn) mn = v;
        if (n == 0 || v > mx) mx = v;
        sum += v;
        ++n;
    }
    double avg() const { return n ? sum / n : 0.0;}
};

//线程采集相关
struct FrameSlot
{
    std::mutex  mtx;
    cv::Mat     img;
    int64_t     t   = 0;
    uint64_t    seq = 0;

    //采集线程，写入像素
    void write(cv::Mat m, int64_t tick)
    {
        std::lock_guard<std::mutex> lock(mtx);
        img = std::move(m);
        t   = tick;
        ++seq;
    }

    //结果线程，只更新比上次大的
    bool read_newer(uint64_t &last_seq, cv::Mat &out, int64_t &tick)
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (seq == last_seq) return false;
        last_seq    = seq;
        out         = std::move(img);
        tick        = t;
        return true;
    }

    //结果线程，收尾判断
    bool has_newer(uint64_t last_seq)
    {
        std::lock_guard<std::mutex> lock(mtx);
        return seq != last_seq;
    }
};

int main(int argc, char **argv)
{
    //参数检测
    if (argc < 2 || argc > 24)
    {
        printf("Usage: %s <model_path> <image_path|video|/dev/videoN> \n", argv[0]);
        return -1;
    }

    int max_frames  = 3;
    int disp_ms     = 500;
    int cam_w       = 640;
    int cam_h       = 480;
    int cam_fps     = 120;
    bool quiet      = true;
    bool live       = false;                //--live:强制每秒状态行(默认在终端里跑就自动开)
    bool show    = true;

    //模型读取
    rkYolov5s model(argv[1]);

    if (model.init(nullptr, false) != 0)
    {
        printf("model init failed\n");
        return -1;
    }

    //时间记录
    const double freq = cv::getTickFrequency();
    StageStat st_read, st_infer;

    //输入数据格式判断
    const bool is_cam = (strncmp(argv[2], "/dev/video", 10) == 0);  //摄像头设备走v4l2
    cv::Mat prode = is_cam ? cv::Mat() : cv::imread(argv[2]);

    cv::VideoCapture cap;
    bool is_video = false;

    if (prode.empty())
    {
        if (is_cam)
        {
            //摄像头接入与缓存数实测
            if (!cap.open(argv[2], cv::CAP_V4L2))   {printf("摄像头打不开：%s\n", argv[2]); return -1;}
            const int cam_buf = 2;
            cap.set(cv::CAP_PROP_BUFFERSIZE, cam_buf);
            cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M','J','P','G'));
            cap.set(cv::CAP_PROP_FRAME_WIDTH,   cam_w);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT,  cam_h);
            cap.set(cv::CAP_PROP_FPS,           cam_fps);
            const int fc = (int)cap.get(cv::CAP_PROP_FOURCC);
            printf("[cam] %s 实际：%dx%d %.1f fps '%c%c%c%c' (buffer=%d)\n",
                    argv[2], (int)cap.get(cv::CAP_PROP_FRAME_WIDTH),  (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT),
                    cap.get(cv::CAP_PROP_FPS),
                    (char)(fc & 0xFF), (char)((fc >> 8) & 0xff), (char)((fc >> 16) & 0xFF), (char)((fc >> 24) & 0xFF),
                    cam_buf);
        }
    }

    //三线程流水
    rknnPool<rkYolov5s, cv::Mat, FrameResult> pool(argv[1], 3);
    if (pool.init() != 0)   { printf("pool init failde\n"); return -1; }
    pool.setMaxPending(3);
    pool.setVerbose(!quiet);

   //最新帧槽+采集线程
   FrameSlot slot;
   std::atomic<bool> running{true};
   std::atomic<bool> eos{false};
   size_t   captured = 0;

   std::thread capThread([&]()
        {
            while (running)
            {
                cv::Mat img;
                if (!cap.read(img))
                {
                    if (!is_cam) { eos = true; break; }

                    printf("[cam] 读帧失败，尝试重连。。。\n");
                    fflush(stdout);
                    bool ok = false;
                    for (int k = 0; k < 30 && running; k++)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                        cap.release();
                        if (cap.open(argv[2], cv::CAP_V4L2))
                        {
                            cap.set(cv::CAP_PROP_BUFFERSIZE, 2);
                            cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M','J','P','G'));
                            cap.set(cv::CAP_PROP_FRAME_WIDTH,  cam_w);
                            cap.set(cv::CAP_PROP_FRAME_HEIGHT, cam_h);
                            cap.set(cv::CAP_PROP_FPS,          cam_fps);
                            ok = true;
                            break;
                        }
                    }
                    if (!ok) { eos = true; break; }
                    printf("[cam] 重连成功，继续\n");
                    fflush(stdout);
                    continue;
                }
                slot.write(std::move(img), cv::getTickCount());
                ++captured;
            }
        });

    //结果线程
    std::deque<int64_t> ts;     //旁路时间帧
    StageStat st_lat;           //端到端延迟统计
    uint64_t last_seq = 0;
    size_t   results  = 0;
    const int64_t t_start = cv::getTickCount();
    const bool live_on = live || isatty(fileno(stdout));    //终端跑
    int64_t t_live = cv::getTickCount();
    float   l_cx   = 0, l_cy = 0, l_r = 0;  //最近一次检测
    bool    l_det  = false, l_pred = false;
    size_t  cap_prev = 0, res_prev = 0;     //上一秒计数
    size_t  hb_prev  = 0;                   //上次心跳帧数
    int64_t t_hb     = t_start;             //上次心跳时刻

    //显示线程
    FrameSlot dslot;
    std::atomic<bool> disp_run{true};
    size_t shown = 0;

    std::thread dispThread([&]()
    {
        uint64_t d_last = 0;
        cv::Mat  dimg;
        int64_t  dt     = 0;
        size_t   vsave  = 0;
        int64_t  t_show = 0;
        while (disp_run)
        {
            if (dslot.read_newer(d_last, dimg,dt))
            {
                if (show)
                {
                    //限30fps
                    const int64_t tn = cv::getTickCount();
                    if (tn - t_show >= (int64_t)(freq / 30.0))
                    {
                        t_show = tn;
                        try
                        {
                            cv::imshow("rknn-ball", dimg);
                            cv::waitKey(1);
                            ++shown;
                        }
                        catch(const cv::Exception &e)
                        {
                            printf("[warn] imshow 失败 （%s）\n", e.what());
                            show = false;
                        }
                    }
                }

            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        if (show)   { try { cv::destroyAllWindows(); } catch (...) {} }
    });

    //开始运行
    while (running)
    {
        bool did = false;

        //喂
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

        //收

        FrameResult r;
        if (pool.get_try(r) == 0)
        {
            const int64_t t0 = ts.front(); ts.pop_front();
            st_lat.add((cv::getTickCount() - t0) * 1000.0 / freq);
            ++results;

            //长跑心跳测帧率
            if (results % 3000 == 0)
            {
                const int64_t tn = cv::getTickCount();
                const double  e1 = (tn - t_start) / freq;
                const double  sg = (tn - t_hb)  / freq;
                printf("[hb] %u 帧 | 本段 %.1f fps | 累计 %.1f fps | 延迟 avg %.2f / max %.2f ms\n",
                        (unsigned)results, (results - hb_prev) /sg, results / e1, st_lat.avg(), st_lat.mx);
                hb_prev = results; t_hb = tn;
                fflush(stdout);
            }

            //预测部分+串口

            //每一秒的状态
            const int64_t tn = cv::getTickCount();
            if ((tn - t_live) >= (int64_t)freq)
            {
                const double sec    = (tn - t_live) / freq;         //据上次打印的秒数
                const double icap   = (captured - cap_prev) / sec;  //本秒采集fps
                const double ires   = (results - res_prev) / freq;  //本秒结果fps
                t_live = tn; cap_prev = captured; res_prev = results;
                char lbuf[256];
                if (l_det)
                {
                    snprintf(lbuf, sizeof lbuf,
                                     "[live] 采集 %.0f | 结果 %.0f fps（瞬时）| 延迟 %.1f/%.1f ms | #%u | cand %d | det (%.0f,%.0f) r=%.0f | pred %d",
                                     icap, ires, st_lat.avg(), st_lat.mx,
                                     (unsigned)results, (int)r.balls.size(), l_cx, l_cy, l_r, (int)l_pred);

                }
                else
                {
                    snprintf(lbuf, sizeof lbuf,
                                     "[live] 采集 %.0f | 结果 %.0f fps（瞬时）| 延迟 %.1f/%.1f ms | #%u | cand %d | 无球",
                                     icap, ires, st_lat.avg(), st_lat.mx,
                                     (unsigned)results, (int)r.balls.size());
                }
                printf("\r%-130s", lbuf);
                fflush(stdout);
            }

            //画框
            for (int k = 0; k < (int)r.balls.size(); k++)
            {
                const vb_ball_t &b = r.balls[k];
                const cv::Point c((int)(b.cx + 0.5f), (int)(b.cy + 0.5f));
                cv::circle(r.image, c, (int)(b.radius + 0.5f), cv::Scalar(0, 255, 0), 3);

            }

            //预测可视化

            dslot.write(std::move(r.image), 0);

            did = true;
        }

        //收尾,采集结束+池空+处理完
        if (eos && pool.pending() == 0 && !slot.has_newer(last_seq)) break;

        //无事睡眠
        if (!did)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }


    }

    running     = false;
        disp_run    = false;
        capThread.join();
        dispThread.join();

        if (live_on) printf("\n");      //给 [live] 的 \r 行收个尾

        //汇总
        const double wall = (cv::getTickCount() - t_start) * 1000.0 / freq;
        printf("\n=========== 流水线汇总（--pipe）===========\n");
        printf("  采集 %u 帧   结果 %u 帧   墙钟 %.2f s\n",
               (unsigned)captured, (unsigned)results, wall / 1000.0);
        printf("  结果吞吐 %.1f fps   采集吞吐 %.1f fps\n",
               results * 1000.0 / wall, captured * 1000.0 / wall);
        printf("  端到端延迟 ms: 平均 %.2f  最小 %.2f  最大 %.2f\n", st_lat.avg(), st_lat.mn, st_lat.mx);
        printf("  拒收 %u 帧   残余在途 %u\n", (unsigned)pool.dropped(), (unsigned)pool.pending());
        if (show) printf("  实时窗口刷新 %u 次（--show）\n", (unsigned)shown);
        else      printf("  显示存图 %u 张(间隔 %d ms)\n", (unsigned)shown, disp_ms);



    return 0;
}