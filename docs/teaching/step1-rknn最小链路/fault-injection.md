# step1 · 故障注入（两款 · 需要板子跑）

> ⚠️ 这两个项目的 bug 在**运行期**才暴露（需要 NPU），必须在**板子上**编译运行：
> 教学材料随 `sync.ps1` 已同步到板子仓库，两个小项目可直接在板子上构建。
> 流程照旧：读症状 → 填工作表 → 排查 → 对照 `answers/step1-rknn最小链路/fault-injection-答案.md`。

---

## 故障 1 · 维度读反（打印出 `channel=640, height=3`）

- **坏版本**：`docs/teaching/fault-injection/step1-01-维度读反/`
- **症状描述**：程序能初始化成功、能查到 attr，但打印的输入尺寸**极其诡异**
  （一个"640×640、3 通道"的模型被打成了 `channel=640, width=640, height=3`），
  随后要么拒绝处理图像，要么 `rknn_inputs_set` 行为异常。
- **你的目标**：定位根因（一句话）+ 修复 + 解释"为什么字节乘积一样、程序还是全错"。

### 板上构建命令

```bash
ssh board
cd ~/myproj/docs/teaching/fault-injection/step1-01-维度读反
cmake -B build -S . && cmake --build build -j4
./build/dims ~/myproj/install/my_rknn_yolov5_demo_aarch64/model/RK3588/best.rknn
```

### 排查工作表（写在这里，别翻答案）

| 我的假设 | 判别实验 | 预期结果 |
|---|---|---|
|  |  |  |
|  |  |  |
|  |  |  |

---

## 故障 2 · 内存只涨不落（跑着跑着 VmRSS 一路爬升）

- **坏版本**：`docs/teaching/fault-injection/step1-02-内存只涨不落/`
- **症状描述**：程序连续推理 150 帧，每 30 帧打印一次 `VmRSS`——
  第一行很稳，之后**每帧涨约 2 MB，从不回落**。推理结果本身完全正确。
- **你的目标**：定位根因（指出哪一站的资源没还）+ 修复 + 用数据证明修复有效。

### 板上构建命令

```bash
ssh board
cd ~/myproj/docs/teaching/fault-injection/step1-02-内存只涨不落
cmake -B build -S . && cmake --build build -j4
./build/leak ~/myproj/install/my_rknn_yolov5_demo_aarch64/model/RK3588/best.rknn
```

### 排查工作表（写在这里，别翻答案）

| 我的假设 | 判别实验 | 预期结果 |
|---|---|---|
|  |  |  |
|  |  |  |

---

> 参考线索：两个 bug 都违背了 `concept-map.md` 第三节/第二节里标粗的某个"必须"。
