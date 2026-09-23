# 教学工具 · 张量属性探针（probe_attrs）

打印任意 `.rknn` 模型的输入/输出属性：dims、fmt、type、zp、scale、size*（不跑推理）。

## 在板子上构建运行

```bash
ssh board
cd ~/myproj/docs/teaching/tools
cmake -B build -S . && cmake --build build -j4

INST=~/myproj/install/my_rknn_yolov5_demo_aarch64
./build/probe_attrs $INST/model/RK3588/best.rknn
./build/probe_attrs $INST/model/RK3588/yolov5s-640-640.rknn
```

> 提示：教学材料里所有"属性类数字"都应来自这个工具的实测输出（数字必出自实测/00 手册）。
