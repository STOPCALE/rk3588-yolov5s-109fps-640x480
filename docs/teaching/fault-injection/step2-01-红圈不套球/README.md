# step2 故障练习 · 「红圈不套球」（需要板子）

独立小项目，**在板子上构建运行**（用了 OpenCV）。

```bash
ssh board
cd ~/myproj/docs/teaching/fault-injection/step2-01-红圈不套球
cmake -B build -S . && cmake --build build -j4
./build/restore ~/testimg/vb640.jpg out640.png
./build/restore ~/testimg/vb1.jpg   out720.png     # 换一张不同尺寸的图对比
```

把输出图取回 PC 查看（PowerShell）：

```powershell
scp board:~/myproj/docs/teaching/fault-injection/step2-01-红圈不套球/out640.png .
```

观察：绿圈（正确位置）与红圈（"还原后"位置）为什么对不上？**偏移的形态**是什么？

> 工作表在 `docs/teaching/step2-letterbox坐标还原/fault-injection.md`；
> 卡壳按 `docs/teaching/step2-letterbox坐标还原/hints.md` 分级取提示。
