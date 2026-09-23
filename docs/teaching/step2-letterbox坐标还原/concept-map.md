# step2 · letterbox 坐标还原（概念地图）

> 目标：掌握「原图 → 模型输入」的正向 letterbox 与「模型坐标 → 原图坐标」的还原公式，
> 并理解漏掉还原 = **静默错误**（不报错、结果全错）的工程含义。
>
> 参考实现：`src/rkYolov5s.cc` 的 `letterbox()` 与 `infer()` 里对 `vb_to_original` 的调用；
> 实测数据：00 §6 D（B7-0）与 §6 G（B7-3）。

---

## 一、正向：原图 → 640×640（四步）

$$scale = \min\left(\frac{640}{W},\frac{640}{H}\right) \quad\text{（取小值，保证整个画面装得下）}$$

| 步 | 公式 | vb1 (720×1280) 实例 |
|---|---|---|
| 1 缩放比 | `scale = min(640/W, 640/H)` | min(0.889, 0.5) = **0.5** |
| 2 新尺寸 | `new_w=W·scale, new_h=H·scale` | 360×640 |
| 3 补边总量 | `dw=640−new_w, dh=640−new_h`；`left=dw/2, top=dh/2` | dw=280, dh=0 → **left=140, top=0** |
| 4 填充 | `copyMakeBorder(top, bottom, left, right)`（**注意参数顺序**） | 左右各 140 灰边 |

**实测校验表（00 §6 D）**：vb1 → scale 0.5、pads (140,140,0,0)，**预测=实测** ✅；
vb2 (1706×1279) → 0.375147、(0,0,80,81)；vb3 (1279×1706) → 0.375147、(80,81,0,0)。
（vb2/vb3 的 1px 差是 int 截断舍入，见手算题 3）

## 二、反向：模型坐标 → 原图坐标（两步，顺序不能反）

$$x_{orig} = \frac{x_{model} - pads.left}{scale} \qquad y_{orig} = \frac{y_{model} - pads.top}{scale}$$

**先减 pad，再除 scale。** 反过来（先除再减）或漏减 pad → 系统性偏移。

## 三、快慢路径（为什么实际场景只有 0.11 ms）

| 场景 | scale | resize? | preprocess |
|---|---|---|---|
| **640×480 → 640×640**（摄像头真实场景） | **1.0** | **跳过**（原尺寸吻合、直接引用 buf） | **0.11 ms** |
| vb1 720×1280 | 0.5 | 要 | 0.63 ms |
| 数据集 1706 大图 | 0.375 | 要（读 6.5 MB 源像素） | 5.15 ~ 6.3 ms |

## 四、两个经典陷阱（都真实发生过）

1. **漏调 `vb_to_original`**（B7-3 的坑）：编译通过、不报错，但结果坐标全是"模型坐标系"的；
   对 vb2/vb3 这种 scale≈0.375 的图，检测值/真值恒等于 **1/scale ≈ 2.666** ——
   唯一的线索就是**这个恒定比例**。**必须把它列为验收项**。
2. **int 截断**：`(int)(1706×0.375147)=639.99→639`? 实测是 640；
   反正 **±1px 级别的舍入**是预期内，不影响正确性（实测确认）。

## 五、参考实现映射（写前只准看"位置"）

| 你的任务 | 参考位置 |
|---|---|
| letterbox 四步 | `rkYolov5s.cc` `letterbox()` |
| 快路径判别 | `rkYolov5s::infer()` 开头（尺寸吻合 → pads=0, scale=1, 不拷贝） |
| 还原调用位置 | `rkYolov5s::infer()` 中检测结果转回原图处（**这行最容易漏**） |

## 六、逐行预检表（先猜后看）

| 代码行 | 我的预测 |
|---|---|
| `float sw = (float)target.width / src.cols;` | 为什么先转 float？ |
| `int left = dw / 2;` `pads.right = dw - left;` | 奇数宽度时左右差几像素？影响什么？ |
| `cv::resize(src, scaled, cv::Size(new_w, new_h));` | 跳过条件是什么？（三连判） |
| `x = (x_model - pads.left) / scale;` | 如果 scale=0.5、pads.left=140，x_model=400 → x_orig=? |
| 还原应该在 NMS 之前还是之后？ | （提示：花 0.043ms 在模型系里 NMS，还是先还原再 NMS？） |
