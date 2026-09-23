# step4 · 提示分级卡（卡壳时翻，不卡不看）

> L3 使用规则不变：翻到 L3 ＝ 当天合上资料凭记忆重做 + 登记到附录 E。

---

## L1 · 方向（一句话指路）

本步所有怪现象先分两类：
- **代码/逻辑**（循环写错、参数没设）→ 查程序；
- **系统状态**（频率没锁、线程被迁移、外部节流）→ 查环境。
**"数字不合手算"时，先怀疑环境，再怀疑代码。**

## L2 · 检查点

| 症状 | 先查什么 |
|---|---|
| `read + infer` ≠ 单帧工作量，且恒定 | 被节流钉死（式 17.7）→ 是不是要 `sync=false` |
| run 比基准慢 ~19% | dmc 锁了吗？（`lock-freq.sh`） |
| preprocess 出现台阶（×1.86） | 线程被迁到 A55 → `taskset -c 4-7` |
| 只解出 ~15% 帧就停 | FFMPEG 后端 bug → 显式 `CAP_GSTREAMER` + 装 libav |
| 相机实测 ~62fps（标称 120） | `CAP_PROP_BUFFERSIZE=1` 的 V4L2 时序问题 → 改 2 |
| read 随文件线性放大 | 它就是解码成本（式 16），别怪存储 |
| 硬解了反而 37ms/帧 | `videoconvert` 在搬 DMA-BUF（123 ns/px） |

## L3 · 关键命令与片段（翻到此级 = 合上重做 + 登记）

```bash
# 定频三件套
ssh -t board "sudo bash ~/myproj/tools/lock-freq.sh lock"
ssh board "bash ~/myproj/tools/lock-freq.sh"        # 复核
ssh board "taskset -c 4-7 ./app ..."                # 绑大核
```

```cpp
// 全速喂帧（绕过实时节流）
char pipeline[1024];
snprintf(pipeline, sizeof pipeline,
         "filesrc location=\"%s\" ! qtdemux ! h264parse ! avdec_h264 ! "
         "videoconvert ! video/x-raw,format=BGR ! appsink sync=false", path);
cap.open(pipeline, cv::CAP_GSTREAMER);
```

```cpp
// 相机：缓冲数 =2（=1 会每两帧丢一帧）
cap.set(cv::CAP_PROP_BUFFERSIZE, 2);
```

构建运行故障小项目（需摄像头）：

```bash
cd ~/myproj/docs/teaching/fault-injection/step4-01-相机掉一半
cmake -B build -S . && cmake --build build -j4
./build/camfps /dev/video0
```
