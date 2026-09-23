# step1 · 提示分级卡（卡壳时翻，不卡不看）

> L3 使用规则不变：翻到 L3 ＝ 当天合上资料凭记忆重做 + 登记到附录 E。

---

## L1 · 方向（一句话指路）

链路只有两段：**init（建好上下文、问清属性）→ infer（喂、推、取、还）**。
任何"怪现象"先问：**是属性没问清（init 段），还是资源没还上（infer 段）？**

- 打印值诡异 → 查 init 段 attr 的**读法**
- 内存涨 / 越跑越慢 → 查 infer 段 get/release 的**配对**

## L2 · 检查点（先查这个）

| 症状 | 先查什么 |
|---|---|
| 打印出 `channel=640, width=3` | dims 读法按 `fmt` 分支了吗？（本项目输入 NHWC、输出 NCHW） |
| `rknn_query` 返回负值 | `attr.index = i` 写了吗？结构体清零了吗？ |
| `inputs_set` 报尺寸错 | `inputs[0].size` 的四则运算（w×h×c）和 buf 指向的数据一致吗？ |
| 输出全零/乱 | `rknn_outputs_get` 的参数顺序？want_float 参数？ |
| VmRSS 单调上涨 | 每个 `outputs_get` 是否都有一个 `outputs_release`（含失败路径）？ |
| init 直接失败 | SDK 版本 vs 驱动版本；模型文件是不是这个板子的架构（aarch64） |

## L3 · 关键片段（翻到此级 = 合上重做 + 登记）

```c
// ① 查询前的铁律
input_attrs[i].index = i;                       // 不写这行→查询结果无意义
ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &input_attrs[i], sizeof(rknn_tensor_attr));

// ② NCHW / NHWC 分支（本项目：输入 fmt=1(NHWC)、输出 fmt=0(NCHW)→两处都不能写死）
if (input_attrs[0].fmt == RKNN_TENSOR_NCHW)
{ channel = dims[1]; height = dims[2]; width = dims[3]; }
else
{ height = dims[1]; width = dims[2]; channel = dims[3]; }

// ③ 配对铁律（成功与失败路径都要还）
ret = rknn_outputs_get(ctx, io_num.n_output, outputs, nullptr);
if (ret != 0) return result;            // ← 失败路径别忘了对齐释放逻辑
/* ...消费 outputs... */
rknn_outputs_release(ctx, io_num.n_output, outputs);
```

板上构建故障小项目（任意目录通用）：

```bash
cd <故障目录> && cmake -B build -S . && cmake --build build -j4
./build/<可执行名> ~/myproj/install/my_rknn_yolov5_demo_aarch64/model/RK3588/best.rknn
```
