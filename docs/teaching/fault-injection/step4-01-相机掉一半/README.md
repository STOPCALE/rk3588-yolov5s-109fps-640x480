# step4 故障练习 · 「相机掉一半」（需要 USB 摄像头）

独立小项目，**在板子上构建运行**。需要 `/dev/video0` 挂着 USB 摄像头
（B11 同款：640×480 MJPG@120）；没摄像头时先跳过，设备就绪后补做。

```bash
ssh board
cd ~/myproj/docs/teaching/fault-injection/step4-01-相机掉一半
cmake -B build -S . && cmake --build build -j4
./build/camfps /dev/video0
```

程序采集 600 帧并打印帧率、read 平均/最小/最大。把它当作陌生人写的工具来排查。

> 工作表在 `docs/teaching/step4-视频循环计时/fault-injection.md`；
> 卡壳按 `docs/teaching/step4-视频循环计时/hints.md` 分级取提示。
