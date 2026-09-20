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
#include "rknnPool.hpp" //模型池

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
    if (argc < 3 || argc > 7)
    {
        printf("Usage: %s <model_path> <image_path> [frames=3] [--quiet] [--fast] [--pipe]\n", argv[0]);
        return -1;
    }

    //可选参数，第3-4位放循环次数或者quiet，fast顺序随意
    int max_frames  = 3;
    bool quiet      = false;
    bool fast       = false;    //全速喂帧
    bool pipe       = false;    //三线程流水线模式
    for (int i =3; i < argc; i++)
    {
        if      (strcmp(argv[i], "--quiet") == 0)    quiet = true;
        else if (strcmp(argv[i], "--fast")  == 0)    fast  = true;
        else if (strcmp(argv[i], "--pipe")  == 0)    pipe  = true;
        else                                    max_frames = atoi(argv[i]);
    }
    if (max_frames <= 0) max_frames = 3;    //防呆
    printf("frame = %d quiet = %d fast = %d pipe = %d\n", max_frames, (int)quiet, (int)fast, (int)pipe);     //回显


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

    //判断第二个是图片还是视频
    cv::Mat prode = cv::imread(argv[2]);

    cv::VideoCapture cap;   //播放器：有当前位置，能一帧一帧走
    bool is_video = false;

    if (prode.empty())
    {
        if (fast)
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
            if (pool.get(r) == 0)
            {
                const int64_t t0 = ts.front(); ts.pop_front();
                st_lat.add((cv::getTickCount() - t0) * 1000.0 / freq);
                ++results;

                //画框
                for (int k = 0; k < (int)r.balls.size(); k++)
                {
                    const vb_ball_t &b = r.balls[k];
                    const cv::Point c((int)(b.cx + 0.5f), (int)(b.cy + 0.5f));
                    cv::circle(r.image, c, (int)(b.radius + 0.5f), cv::Scalar(0, 255, 0), 3);
                }

                did = true;
                if ((int)results >= max_frames) break;
            }

            //收尾：采集结束+池空+没新帧+全处理完了
            if (eos && pool.pending() == 0 && !slot.has_newer(last_seq)) break;

            //没事干睡1ms
            if (!did)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        running = false;
        capThread.join();

        //汇总
        const double wall = (cv::getTickCount() - t_start) * 1000.0 / freq;
        printf("\n=========== 流水线汇总（--pipe）===========\n");
        printf("  采集 %u 帧   结果 %u 帧   墙钟 %.2f s\n",
               (unsigned)captured, (unsigned)results, wall / 1000.0);
        printf("  结果吞吐 %.1f fps   采集吞吐 %.1f fps\n",
               results * 1000.0 / wall, captured * 1000.0 / wall);
        printf("  端到端延迟 ms: 平均 %.2f  最小 %.2f  最大 %.2f\n", st_lat.avg(), st_lat.mn, st_lat.mx);
        printf("  拒收 %u 帧   残余在途 %u\n", (unsigned)pool.dropped(), (unsigned)pool.pending());
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
