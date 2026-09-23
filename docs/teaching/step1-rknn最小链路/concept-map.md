# step1 · RKNN 最小链路（概念地图）

> 目标：掌握「一个模型从文件到推理结果」的最短链路——**init 七步 + infer 四件套**，
> 并在板上亲手跑出一次真实推理（infer ≈ 22.4 ms）。本步不涉及 letterbox/后处理（分别在 step2/step3）。
>
> 参考实现：`src/rkYolov5s.cc`（init 与 infer）；文档：`docs/00-工程速查手册.md` §2。

---

## 一、init 七步（状态机）

```
read_model(文件→内存) → rknn_init(创建上下文) → rknn_set_core_mask(绑核)
   → 查询阶段: SDK版本 → IN_OUT_NUM → INPUT_ATTR → OUTPUT_ATTR(含量化参数)
```

| # | 调用 | 作用 | 典型坑 |
|---|---|---|---|
| 1 | `read_model()` | 把 .rknn 读进 `unsigned char*` | **rknn_init 之后才能 free**（init 内部拷贝） |
| 2 | `rknn_init(&ctx, data, size, 0, nullptr)` | 创建上下文本 | 失败返回负值——必须查 `ret` |
| 3 | `rknn_set_core_mask(ctx, mask)` | 绑单个 NPU 核（多实例并行用） | `RKNN_CORE_MASK` 环境变量可覆盖 |
| 4 | `rknn_query(RKNN_QUERY_SDK_VERSION)` | 打印 SDK/驱动版本（本机实测：sdk 1.5.2 / driver 0.9.8） | 工具链与运行时版本不匹配 → init 失败 |
| 5 | `rknn_query(RKNN_QUERY_IN_OUT_NUM)` | 输入/输出**个数** | 后续所有循环的边界都来自它 |
| 6 | `rknn_query(RKNN_QUERY_INPUT_ATTR)` | 输入张量属性（dims/fmt） | **必须先填 `attr.index=i`** 再 query |
| 7 | `rknn_query(RKNN_QUERY_OUTPUT_ATTR)` | 输出属性（dims/zp/scale） | zp/scale 在 init 时抄一份复用（不要每帧查） |

## 二、infer 四件套（每帧）

```
rknn_inputs_set(喂) → rknn_run(推) → rknn_outputs_get(取) → 【必配】rknn_outputs_release(还)
```

- `rknn_input` 的字段：`index=0`、`type=RKNN_TENSOR_UINT8`、`fmt=RKNN_TENSOR_NHWC`、
  `size = width*height*channel`、`buf` 指向像素数据。**这些在 init 末尾配好，infer 只改 buf**。
- `rknn_outputs_get` 不配 `rknn_outputs_release` → **每帧泄漏＝输出总量**（best ≈0.144 MB、yolov5s ≈2.14 MB），且单调累积。
- 失败路径同样要 release（主工程 §1.5 已修）。

## 三、张量属性速查（rknn_tensor_attr 的关键字段）

| 字段 | 含义 | 本项目实测值（best.rknn / yolov5s-640） |
|---|---|---|
| `n_dims` | 维数 | 4 |
| `dims[]` | 各维大小 | **输入** `[1,640,640,3]`；**输出**（best）`[1,18,80,80] [1,18,40,40] [1,18,20,20]`（18 通道 = 3 锚框 × 6 项 × 单类别；yolov5s 为 255 通道 ×3 头） |
| `fmt` | 布局 | 输入 **NHWC(1)**；输出 **NCHW(0)**——两种读法**都要会**，必须按 fmt 分支 |
| `type` | 数据类型 | 输入 UINT8；输出 INT8（量化） |
| `zp` / `scale` | 量化零点/系数 | `zp=-128`，`scale=0.00392157（=1/255）` → 真实值 $=(q-zp)\times scale$ |
| `size` / `size_with_stride` | 有效字节 / 含对齐填充 | best 输出0：115,200 vs **204,800**（驱动对齐会放大 78%） |

**NHWC 读法**：`h=dims[1], w=dims[2], c=dims[3]`（本项目**输入**）；
**NCHW 读法**：`c=dims[1], h=dims[2], w=dims[3]`（本项目**输出**）。
（输入读错的后果：打印出 `channel=640, width=3` 这种诡异值——见故障注入）

## 四、参考实现映射（写代码前只准看"位置"，不准看"内容"）

| 你的任务 | 参考位置（写完再看） |
|---|---|
| init 七步 | `src/rkYolov5s.cc` `rkYolov5s::init()` |
| 输入尺寸推导 + inputs[0] 配置 | 同函数末尾 |
| infer 四件套 | `rkYolov5s::infer()` 中 `rknn_inputs_set`~`rknn_outputs_release` 段 |
| 资源释放 | 析构函数 |

## 五、逐行预检表（先猜后看）

| 代码行（本步你要写的） | 我的预测 |
|---|---|
| `ret = rknn_init(...); if (ret < 0) return -1;` | RKNN 失败码是"负值"还是"非 0"？ |
| `ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));` | `sizeof` 该填结构体还是指针？ |
| `inputs[0].size = width * height * channel;` | 对 640×640×3 等于多少字节？ |
| `rknn_outputs_get(ctx, io_num.n_output, outputs, nullptr);` | 第三个参数传 nullptr 合法吗？ |
| `rknn_outputs_release(ctx, io_num.n_output, outputs);` | 漏掉它会发生什么？ |

## 六、六条硬约定（本步相关）

1. attr 查询前必须 `attr.index = i`；2. `io_num.n_output` 校验后再循环（本项目=3）；
3. 每次 get 必须配 release（成功/失败路径都要）；4. 量化参数 init 时抄好、infer 不查询。
