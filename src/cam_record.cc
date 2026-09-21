// =============================================================================
//  cam_record（B12）—— 摄像头录像工具：拍素材 / 建训练数据集用（不跑模型）
// -----------------------------------------------------------------------------
//  特点：
//    · 640×480 MJPG @120 档（本机实测 ~109.5 fps）；先"预跑"0.6 秒估算真实帧率，
//      再按真实帧率声明给编码器（录出来的视频播放速度和现实一致）
//    · 两种模式：
//        mp4 模式（默认)：解码 → 管道给 ffmpeg（x264 ultrafast crf18）→ mp4，通用、体积小
//        --jpg 模式      ：逐帧落盘；若驱动支持 MJPEG 直通（CONVERT_RGB=0），
//                          写出的是**相机原始 JPEG 字节**（零解码、零损失 —— 建数据集首选）
//    · Ctrl+C 优雅停止：管道关闭 → ffmpeg 正常收尾（mp4 一定能播放）
//    · 录完自动写 sidecar：<文件>.txt / <目录>/info.txt（帧数、时长、真实 fps——标注时有用）
//
//  用法（板子上）：
//      cam_record /dev/video0 --sec 60                 # 录 60 秒 → ~/videos/cam_<时间>.mp4
//      cam_record /dev/video0                          # 一直录，Ctrl+C 停
//      cam_record /dev/video0 --jpg --sec 30           # 原始 JPEG 帧序列 → ~/videos/cam_<时间>/
//      cam_record /dev/video0 --out /tmp/a.mp4 --sec 10
//      cam_record /dev/video0 --fps 60 --size 1280x720 # 换档位（默认 640x480@120）
// =============================================================================
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <csignal>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <opencv2/opencv.hpp>

#ifdef _WIN32
#define popen  _popen
#define pclose _pclose
#endif

static volatile sig_atomic_t g_stop = 0;
static void on_stop(int) { g_stop = 1; }

static bool is_jpeg(const cv::Mat &m)
{
    if (m.empty() || m.total() * m.elemSize() < 4) return false;
    const unsigned char *p = m.data;
    return p[0] == 0xFF && p[1] == 0xD8;
}

int main(int argc, char **argv)
{
    const char *dev = "/dev/video0";
    const char *out = nullptr;      // mp4: 文件名；jpg: 目录
    int  sec = 0;                   // 0 = 一直录（Ctrl+C 停）
    int  fps = 120;
    int  w = 640, h = 480;
    int  crf = 18;
    bool jpg = false;

    for (int i = 1; i < argc; i++)
    {
        if      (!strncmp(argv[i], "/dev/", 5))              dev = argv[i];
        else if (!strcmp(argv[i], "--out")  && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--sec")  && i + 1 < argc) sec = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fps")  && i + 1 < argc) fps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &w, &h);
        else if (!strcmp(argv[i], "--crf")  && i + 1 < argc) crf = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--jpg"))                  jpg = true;
        else { printf("未知参数: %s（用法见文件头注释）\n", argv[i]); return -1; }
    }

    // ---- 打开相机（缓冲>=2：实测 =1 会每两帧丢一帧） ----
    cv::VideoCapture cap;
    if (!cap.open(dev, cv::CAP_V4L2)) { printf("打开失败: %s\n", dev); return -1; }
    cap.set(cv::CAP_PROP_BUFFERSIZE, 2);
    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M','J','P','G'));
    cap.set(cv::CAP_PROP_FRAME_WIDTH,  w);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, h);
    cap.set(cv::CAP_PROP_FPS,          fps);
    printf("[rec] %s  %dx%d @%.0f fps  MJPG  (buffer=2)\n",
           dev, (int)cap.get(cv::CAP_PROP_FRAME_WIDTH), (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT),
           cap.get(cv::CAP_PROP_FPS));

    // ---- 预跑 0.6 秒：估真实帧率（标称 120，实测约 109.5，差 9% 会影响播放速度） ----
    cv::Mat frame;
    const double tick = cv::getTickFrequency();
    const int64_t t_pre = cv::getTickCount();
    int pre_n = 0;
    while (pre_n < 66 && !g_stop) { if (cap.read(frame)) ++pre_n; }
    const double est_fps = pre_n / ((cv::getTickCount() - t_pre) / tick);
    printf("[rec] 预跑：%d 帧/%.2fs → 真实帧率 ≈ %.2f fps\n", pre_n,
           (cv::getTickCount() - t_pre) / tick, est_fps);

    // ---- 输出准备 ----
    const char *home = getenv("HOME"); if (!home) home = "/tmp";
    char defdir[600]; snprintf(defdir, sizeof defdir, "%s/videos", home);
    char stamp[32];
    { time_t t = time(nullptr); struct tm *tmv = localtime(&t); strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", tmv); }

    FILE *pipe = nullptr;           // mp4 模式：ffmpeg 管道
    std::string fpath;              // mp4 文件路径
    std::string jdir;               // jpg 目录
    bool raw_ok = false;            // jpg 模式：MJPEG 直通是否可用
    int  sink_err = 0;

    if (jpg)
    {
        if (out && !strstr(out, ".mp4")) jdir = out;
        else { char b[700]; snprintf(b, sizeof b, "%s/cam_%s", defdir, stamp); jdir = b; }
        char cmd[800]; snprintf(cmd, sizeof cmd, "mkdir -p \"%s\"", jdir.c_str()); if (system(cmd) != 0) {}

        cap.set(cv::CAP_PROP_CONVERT_RGB, 0);       // 试 MJPEG 直通
        for (int k = 0; k < 6; k++)
        {
            if (!cap.read(frame)) continue;
            if (is_jpeg(frame)) { raw_ok = true; break; }
        }
        if (!raw_ok) { cap.set(cv::CAP_PROP_CONVERT_RGB, 1); printf("[rec] MJPEG 直通不可用 → 退回解码后写 JPEG(q95)\n"); }
        else         printf("[rec] MJPEG 直通可用 → 写出相机原始 JPEG（零损失）\n");
        printf("[rec] 输出目录: %s（Ctrl+C 停止）\n", jdir.c_str());
    }
    else
    {
        if (out && strstr(out, ".mp4")) fpath = out;
        else { char b[700]; snprintf(b, sizeof b, "%s/cam_%s.mp4", defdir, stamp); fpath = b; }
        const size_t sl = fpath.find_last_of('/');
        std::string d = (sl == std::string::npos) ? std::string(".") : fpath.substr(0, sl);
        char cmd0[800]; snprintf(cmd0, sizeof cmd0, "mkdir -p \"%s\"", d.c_str()); if (system(cmd0) != 0) {}

        char cmd[900];
        snprintf(cmd, sizeof cmd,
                 "ffmpeg -y -loglevel error -f rawvideo -pix_fmt bgr24 -s %dx%d -framerate %.3f -i pipe:0 "
                 "-c:v libx264 -preset ultrafast -crf %d -pix_fmt yuv420p \"%s\"",
                 w, h, est_fps, crf, fpath.c_str());
        pipe = popen(cmd, "w");
        if (!pipe) { printf("无法启动 ffmpeg（装了没？）\n"); return -1; }
        printf("[rec] 输出: %s（%.1f%% 真实速率；Ctrl+C 停止）\n", fpath.c_str(), est_fps / fps * 100.0);
    }

    signal(SIGINT,  on_stop);
    signal(SIGTERM, on_stop);

    // ---- 主循环 ----
    const int64_t t0 = cv::getTickCount();
    int64_t t_print = t0;
    size_t frames = 0;
    cv::Mat last;
    while (!g_stop)
    {
        if (!cap.read(frame))
        {
            if (++sink_err > 100) { printf("\n[rec] 相机读帧连续失败，中止\n"); break; }
            continue;
        }
        last = frame;
        ++frames;

        if (jpg)
        {
            char fn[800];
            snprintf(fn, sizeof fn, "%s/frame_%06u.jpg", jdir.c_str(), (unsigned)(frames - 1));
            if (raw_ok)
            {
                FILE *f = fopen(fn, "wb");
                if (!f || fwrite(frame.data, 1, frame.total() * frame.elemSize(), f) != frame.total() * frame.elemSize())
                { ++sink_err; }
                if (f) fclose(f);
            }
            else
            {
                std::vector<int> q = {cv::IMWRITE_JPEG_QUALITY, 95};
                if (!cv::imwrite(fn, frame, q)) ++sink_err;
            }
        }
        else
        {
            const size_t bytes = frame.total() * frame.elemSize();
            if (fwrite(frame.data, 1, bytes, pipe) != bytes) { ++sink_err; break; }
        }

        const int64_t tn = cv::getTickCount();
        if (tn - t_print >= (int64_t)tick)
        {
            t_print = tn;
            const double el = (tn - t0) / tick;
            printf("[rec] %6.1fs  %6u 帧  %5.1f fps  %s\n",
                   el, (unsigned)frames, frames / el, g_stop ? "收尾…" : "");
            fflush(stdout);
        }
        if (sec > 0 && (tn - t0) / tick >= sec) break;
    }

    // ---- 收尾 ----
    const double el = (cv::getTickCount() - t0) / tick;
    if (pipe) { pclose(pipe); pipe = nullptr; }    // 关管道 → ffmpeg 收尾（mp4 可播放）

    printf("\n[rec] 完成：%u 帧 / %.1f s → 实际 %.2f fps\n", (unsigned)frames, el, frames / el);
    long long fsz = 0;
    if (jpg)
    {
        printf("[rec] 目录: %s\n", jdir.c_str());
    }
    else
    {
        struct stat st;
        if (stat(fpath.c_str(), &st) == 0) fsz = (long long)st.st_size;
        printf("[rec] 文件: %s  (%.1f MB)\n", fpath.c_str(), fsz / 1048576.0);
    }
    if (sink_err > 0) printf("[rec] ⚠️ 写盘失败 %d 次（磁盘满/管道断？）\n", sink_err);

    // ---- sidecar：给训练/标注用的元数据 ----
    {
        std::string sp = jpg ? (jdir + "/info.txt") : (fpath + ".txt");
        FILE *f = fopen(sp.c_str(), "w");
        if (f)
        {
            fprintf(f, "frames=%u\nelapsed_s=%.3f\nreal_fps=%.3f\n", (unsigned)frames, el, el > 0 ? frames / el : 0.0);
            if (!jpg) fprintf(f, "declared_fps=%.3f\n", est_fps);
            fprintf(f, "mode=%s\nsize=%dx%d\n", jpg ? (raw_ok ? "mjpg-raw" : "jpg-decoded") : "mp4-h264", w, h);
            time_t t = time(nullptr); struct tm *tmv = localtime(&t);
            fprintf(f, "finished=%04d-%02d-%02d %02d:%02d:%02d\n",
                    tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday, tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
            fclose(f);
            printf("[rec] 元数据: %s\n", sp.c_str());
        }
    }
    return 0;
}
