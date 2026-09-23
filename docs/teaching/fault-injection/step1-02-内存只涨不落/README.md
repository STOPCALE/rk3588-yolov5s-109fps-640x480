# step1 故障练习 · 「内存只涨不落」（需要板子）

独立小项目，**在板子上构建运行**（教学材料随 sync 已在板子仓库里）。

```bash
ssh board
cd ~/myproj/docs/teaching/fault-injection/step1-02-内存只涨不落
cmake -B build -S . && cmake --build build -j4
./build/leak ~/myproj/install/my_rknn_yolov5_demo_aarch64/model/RK3588/best.rknn
```

程序长跑 150 帧，每 30 帧打印一次 `VmRSS`。**观察这条曲线的走势**。

> 工作表在 `docs/teaching/step1-rknn最小链路/fault-injection.md`；
> 卡壳按 `docs/teaching/step1-rknn最小链路/hints.md` 分级取提示。
