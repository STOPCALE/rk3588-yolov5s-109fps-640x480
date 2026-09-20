// ===========================================================================
// 摄像头诊断工具（B11）—— 相机接入前的"体检"，不跑模型、不组包
// ---------------------------------------------------------------------------
// 回答四个问题：
//   ① 相机支持什么？    v4l2-ctl --list-formats-ext 原样打印（格式/分辨率/帧率）
//   ② 实际生效什么？    打开后回显实际 WxH / fps / FOURCC（请求值可能与生效值不同）
//   ③ 真实性能如何？    连续采集 N 秒：实际帧率、单帧 read 耗时(含 MJPG 解码)、
//                       逐秒帧率表（"第 1 秒快、后面慢" = 节流/积压的指纹）
//   ④ 画面什么样？      存 3 张样例图（开头/中段/结尾），用来查 FOV/颜色/曝光
//
// 用法（板子上，cwd = 安装目录）：
//   ./cam_probe /dev/video0                        # 默认 MJPG 640x480@120, 8 秒
//   ./cam_probe /dev/video0 --size 1280x720 --fps 60
//   ./cam_probe /dev/video0 --fourcc YUYV --fps 30
//   ./cam_probe /dev/video0 --sec 15 --save /tmp/cam
// 说明：--sec 采集秒数；--save 样例图目录（默认 /tmp/camprobe）
// ===========================================================================
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <opencv2/opencv.hpp>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("用法: %s <设备> [--size WxH] [--fps N] [--fourcc MJPG|YUYV] [--sec N] [--buf N] [--save 目录] [--no-list]\n", argv[0]);
        return -1;
    }
    const char *dev    = argv[1];
    int         w = 640, h = 480, fps = 120, sec = 8;
    const char *fourcc = "MJPG";
    const char *save   = "/tmp/camprobe";
    int         buf    = 2;          // 驱动缓冲数：实测 1 会每两帧丢一帧(120fps->62fps)；0=不设用默认
    bool        list   = true;
    for (int i = 2; i < argc; i++)
    {
        if      (!strcmp(argv[i], "--size")   && i + 1 < argc) sscanf(argv[++i], "%dx%d", &w, &h);
        else if (!strcmp(argv[i], "--fps")    && i + 1 < argc) fps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fourcc") && i + 1 < argc) fourcc = argv[++i];
        else if (!strcmp(argv[i], "--sec")    && i + 1 < argc) sec = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--buf")    && i + 1 < argc) buf = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--save")   && i + 1 < argc) save = argv[++i];
        else if (!strcmp(argv[i], "--no-list"))                list = false;
    }
    if (sec <= 0) sec = 8;

    if (list)
    {
        char cmd[512];
        snprintf(cmd, sizeof cmd, "v4l2-ctl -d %s --list-formats-ext 2>&1", dev);
        printf("===== 相机能力（%s）=====\n", dev);
        (void)system(cmd);
        printf("===== 体检开始 =====\n");
    }

    cv::VideoCapture cap;
    if (!cap.open(dev, cv::CAP_V4L2)) { printf("打开失败: %s\n", dev); return -1; }
    if (buf > 0) cap.set(cv::CAP_PROP_BUFFERSIZE, buf);   // 驱动缓冲数（1=最省延迟但可能丢帧; 0=不设用默认）
    if (strcmp(fourcc, "YUYV") != 0 && strcmp(fourcc, "yuyv") != 0)
        cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    else
        cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('Y', 'U', 'Y', 'V'));
    cap.set(cv::CAP_PROP_FRAME_WIDTH,  w);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, h);
    cap.set(cv::CAP_PROP_FPS,          fps);

    {
        const int  fc = (int)cap.get(cv::CAP_PROP_FOURCC);
        printf("[req] %dx%d@%d %s buf=%d\n", w, h, fps, fourcc, buf);
        printf("[act] %dx%d %.1f fps '%c%c%c%c'\n",
               (int)cap.get(cv::CAP_PROP_FRAME_WIDTH), (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT),
               cap.get(cv::CAP_PROP_FPS),
               (char)(fc & 0xFF), (char)((fc >> 8) & 0xFF), (char)((fc >> 16) & 0xFF), (char)((fc >> 24) & 0xFF));
    }

    // 预热（等自动曝光/白平衡收敛，前几帧不要）
    cv::Mat img;
    for (int i = 0; i < 8; i++) cap.read(img);

    {
        char cmd[600];
        snprintf(cmd, sizeof cmd, "mkdir -p %s", save);
        (void)system(cmd);
    }

    const double  freq = cv::getTickFrequency();
    const int64_t t0   = cv::getTickCount();
    int64_t t_max = 0, t_min = INT64_MAX, t_sum = 0;
    size_t  total = 0, fails = 0;
    std::vector<int> per_sec(sec + 2, 0);
    bool saved_a = false, saved_b = false;
    char fn[700];

    printf("\n[计时] 连续采集 %d 秒...\n", sec);
    while (true)
    {
        const int64_t a = cv::getTickCount();
        if (!cap.read(img))
        {
            if (++fails > 10) { printf("连续读失败，中止\n"); break; }
            continue;
        }
        const int64_t b = cv::getTickCount();
        const int64_t d = b - a;
        if (d > t_max) t_max = d;
        if (d < t_min) t_min = d;
        t_sum += d;
        total++;

        const double el = (b - t0) / freq;
        if (el >= sec) break;

        const int si = (int)el;
        if (si <= sec) per_sec[si]++;

        if (!saved_a)
        {
            snprintf(fn, sizeof fn, "%s/a_%dx%d_%dfps.jpg", save, w, h, fps);
            cv::imwrite(fn, img);
            saved_a = true;
        }
        if (!saved_b && el >= sec / 2.0)
        {
            snprintf(fn, sizeof fn, "%s/b_mid.jpg", save);
            cv::imwrite(fn, img);
            saved_b = true;
        }
    }
    if (!img.empty())
    {
        snprintf(fn, sizeof fn, "%s/c_last.jpg", save);
        cv::imwrite(fn, img);
    }

    const double wall = (cv::getTickCount() - t0) / freq;
    int first = per_sec[0], last = 0;
    for (int s = sec - 1; s >= 0; s--) { if (per_sec[s] > 0) { last = per_sec[s]; break; } }

    printf("\n===== 体检结果 =====\n");
    printf("  时长 %.2f s   帧数 %zu   平均 %.1f fps\n", wall, total, total / wall);
    printf("  read+解码 ms: 平均 %.2f  最小 %.2f  最大 %.2f\n",
           total ? t_sum / (double)total / freq * 1000.0 : 0.0, t_min / freq * 1000.0, t_max / freq * 1000.0);
    printf("  逐秒帧率: ");
    for (int s = 0; s < sec; s++) printf("%d ", per_sec[s]);
    printf("\n  漂移: 首秒 %d -> 末整秒 %d (%+.1f%%)\n",
           first, last, first ? (last - first) * 100.0 / first : 0.0);
    printf("  样例图: %s/a_*.jpg  b_mid.jpg  c_last.jpg\n", save);
    return 0;
}
