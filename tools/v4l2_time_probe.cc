// =============================================================================
//  tools/v4l2_time_probe.cc —— V4L2 内核时间戳诊断工具
//  性质：**诊断工具**（不是学习本体，不进入主链路；为“绝对延迟验证”服务）
//
//  【测什么】
//      ① 每一帧：delta = DQBUF 返回时刻 − 内核时间戳（buf.timestamp）
//         —— “帧在内核完成 → 用户态可见”的等待时间（驱动队列积压的直接测量）
//      ② 相邻内核时间戳的间隔 —— 真实帧周期（微秒级，不受用户态抖动影响）
//      ③ 时间戳类型（MONOTONIC / REALTIME / UNKNOWN）
//
//  【为什么需要它】
//      全链延迟分解：
//        光子 → [曝光+读出+USB] → 内核戳 ts → [队列等待] → 用户态可见 → [推理等] → 结果
//      本工具测“ts → 用户态可见”这一段（目前只有估算：buffer=2 ≈ 2 帧 ≈ 18ms）；
//      最前段“曝光+读出”只能靠外部光参照实验（屏幕时钟 / LED）测。
//
//  【用法】板子上：
//      g++ -O2 -o /tmp/v4l2_time_probe tools/v4l2_time_probe.cc
//      taskset -c 4-7 /tmp/v4l2_time_probe /dev/video0 --sec 5 --buf 2 --fps 120 --csv /tmp/ts2.csv
//      # 对照：--buf 1 / 2 / 4 各跑一次 → 队列深度对“可见延迟”的影响
//
//  【预期】120fps（周期 8.33ms）时：delta 稳态 ≈ 1~2 帧周期（8~17ms，buf=2）；
//          buf=4 会涨到 ~3~4 帧周期。该数字 = “相机段”的下界。
//
//  【跑之前】确认没有别的程序占用相机；测性能数据前先锁频 + taskset。
// =============================================================================
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <ctime>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <linux/videodev2.h>

struct Buf { void *start; size_t length; };

static int64_t now_ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static int xioctl(int fd, unsigned long req, void *arg)
{
    int r;
    do { r = ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
    return r;
}

static void stat_line(const char *name, std::vector<double> &v)
{
    if (v.empty()) { printf("%s: (no data)\n", name); return; }
    double sum = 0;
    for (double x : v) sum += x;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    printf("%-22s avg %9.1f | min %9.1f | p50 %9.1f | p95 %9.1f | max %9.1f   (n=%zu)\n",
           name, sum / n, v.front(), v[n / 2], v[(size_t)(n * 0.95)], v.back(), n);
}

int main(int argc, char **argv)
{
    const char *dev  = "/dev/video0";
    int w = 640, h = 480, fps = 120, sec = 5, nbuf = 2;
    const char *csv = nullptr;

    for (int i = 1; i < argc; i++)
    {
        if      (!strcmp(argv[i], "--sec")  && i + 1 < argc) sec  = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--buf")  && i + 1 < argc) nbuf = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fps")  && i + 1 < argc) fps  = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &w, &h);
        else if (!strcmp(argv[i], "--csv")  && i + 1 < argc) csv  = argv[++i];
        else if (argv[i][0] != '-')                          dev  = argv[i];
    }
    if (sec <= 0) sec = 5;
    if (nbuf < 1) nbuf = 1;

    int fd = open(dev, O_RDWR | O_NONBLOCK);
    if (fd < 0) { perror("open"); return -1; }

    struct v4l2_capability cap;
    memset(&cap, 0, sizeof cap);
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) { perror("QUERYCAP"); return -1; }
    printf("device   : %s\n", dev);
    printf("driver   : %s | card: %s | bus: %s\n", cap.driver, cap.card, cap.bus_info);

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = w;
    fmt.fmt.pix.height      = h;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) == -1) { perror("S_FMT"); return -1; }
    printf("format   : %dx%d  fourcc %c%c%c%c  sizeimage %u\n",
           fmt.fmt.pix.width, fmt.fmt.pix.height,
           (char)(fmt.fmt.pix.pixelformat & 0xFF),
           (char)((fmt.fmt.pix.pixelformat >> 8) & 0xFF),
           (char)((fmt.fmt.pix.pixelformat >> 16) & 0xFF),
           (char)((fmt.fmt.pix.pixelformat >> 24) & 0xFF),
           fmt.fmt.pix.sizeimage);

    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof parm);
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator   = 1;
    parm.parm.capture.timeperframe.denominator = fps;
    if (xioctl(fd, VIDIOC_S_PARM, &parm) == -1) perror("S_PARM(warn)");
    memset(&parm, 0, sizeof parm);
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_G_PARM, &parm) != -1)
        printf("fps      : req %d -> act %.2f\n", fps,
               parm.parm.capture.timeperframe.denominator /
               (double)parm.parm.capture.timeperframe.numerator);

    struct v4l2_requestbuffers rb;
    memset(&rb, 0, sizeof rb);
    rb.count  = nbuf;
    rb.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    rb.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &rb) == -1) { perror("REQBUFS"); return -1; }
    printf("bufs     : req %d -> act %u (mmap)\n", nbuf, rb.count);

    std::vector<Buf> bufs(rb.count);
    for (unsigned i = 0; i < rb.count; i++)
    {
        struct v4l2_buffer b;
        memset(&b, 0, sizeof b);
        b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index  = i;
        if (xioctl(fd, VIDIOC_QUERYBUF, &b) == -1) { perror("QUERYBUF"); return -1; }
        bufs[i].length = b.length;
        bufs[i].start  = mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
        if (bufs[i].start == MAP_FAILED) { perror("mmap"); return -1; }
        if (xioctl(fd, VIDIOC_QBUF, &b) == -1) { perror("QBUF"); return -1; }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) == -1) { perror("STREAMON"); return -1; }

    FILE *fcsv = csv ? fopen(csv, "w") : nullptr;
    if (fcsv) fprintf(fcsv, "n,ts_ns,dq_ns,delta_us,interval_us,bytesused\n");

    std::vector<double> deltas, intervals;
    int64_t prev_ts = 0;
    int n = 0;
    bool flag_printed = false;
    const int64_t t_end = now_ns() + (int64_t)sec * 1000000000LL;

    while (now_ns() < t_end)
    {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        struct timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = 100000;
        int r = select(fd + 1, &fds, nullptr, nullptr, &tv);
        if (r <= 0) continue;

        struct v4l2_buffer b;
        memset(&b, 0, sizeof b);
        b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd, VIDIOC_DQBUF, &b) == -1)
        {
            if (errno == EAGAIN) continue;
            perror("DQBUF");
            break;
        }

        const int64_t t_dq = now_ns();
        const int64_t ts   = (int64_t)b.timestamp.tv_sec * 1000000000LL
                           + (int64_t)b.timestamp.tv_usec * 1000LL;
        const double delta = (t_dq - ts) / 1000.0;
        const double itv   = prev_ts ? (ts - prev_ts) / 1000.0 : 0.0;

        if (!flag_printed)
        {
            const unsigned f = b.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK;
            const char *tn = "UNKNOWN/other";
            if (f == V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC) tn = "MONOTONIC";
            printf("ts type  : %s (raw flag 0x%x)\n", tn, f);
            printf("note     : 若为 MONOTONIC，可直接与 clock_gettime 差值比较\n");
            flag_printed = true;
        }

        deltas.push_back(delta);
        if (prev_ts) intervals.push_back(itv);
        prev_ts = ts;

        if (fcsv)
            fprintf(fcsv, "%d,%lld,%lld,%.1f,%.1f,%u\n",
                    n, (long long)ts, (long long)t_dq, delta, itv, (unsigned)b.bytesused);

        n++;
        xioctl(fd, VIDIOC_QBUF, &b);
    }

    xioctl(fd, VIDIOC_STREAMOFF, &type);
    if (fcsv) fclose(fcsv);

    printf("-----------------------------------------------------------\n");
    printf("frames   : %d in %d s(设定)   buf=%d\n", n, sec, (int)rb.count);
    if (n > 30)
    {
        // 去掉前 30 帧预热后再统计一份（口径：稳态）
        std::vector<double> d2(deltas.begin() + 30, deltas.end());
        std::vector<double> i2(intervals.begin() + 30, intervals.end());
        stat_line("delta(dq-ts) us", d2);
        stat_line("interval us", i2);
    }
    else
    {
        stat_line("delta(dq-ts) us", deltas);
        stat_line("interval us", intervals);
    }
    if (csv) printf("csv      : %s\n", csv);

    for (unsigned i = 0; i < rb.count; i++) munmap(bufs[i].start, bufs[i].length);
    close(fd);
    return 0;
}
