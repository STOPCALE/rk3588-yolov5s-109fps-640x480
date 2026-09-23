# step0 · 验收清单（可测量、可勾选）

> 全部达标 = 本步完成。每一项都写明「怎么验」；不接受「我觉得可以了」。

## 一、构建与工作流

- [ ] `.\tools\check.ps1` → `all N file(s) passed`
- [ ] `.\tools\sync.ps1 -Message "..."` → 五个标志全绿（已提交 / 已推送 / Fast-forward / 编译成功 / 全部完成 🎉）
- [ ] 板上 `grep -o '\-O[0-9]' build/CMakeFiles/<target>.dir/flags.make | sort -u` → 输出 `-O3`
- [ ] 板上 `readelf -d <安装目录>/my_rknn_yolov5_demo | grep -i runpath` → `[$ORIGIN/lib]`
- [ ] 安装目录里 `./my_rknn_yolov5_demo`（空壳版）能启动并打印你的「骨架就绪」信息

## 二、手算命中（对照 hand-calc）

- [ ] 题 1：理论吞吐 ≈ **133 fps**、并行效率 ≈ **93%**
- [ ] 题 5：read 预测 ≈ **2.9 ms**（与实测 2.90 ms 对上）
- [ ] 题 6：`inputs_set` 有效带宽 ≈ **11 GB/s**

## 三、白纸默写（不看任何资料）

- [ ] 写出「日常开发 4 命令」及 cwd 要求
- [ ] 画出 Git 三仓库流向图（含每仓库允许/禁止操作）
- [ ] 写出 CMake 四阶段 + 「改了 CMakeLists 后必须重跑哪两步」
- [ ] 说出 6 条不可动约定中的 ≥4 条

## 四、故障注入

- [ ] 故障 1：一句话根因 + 一条验证命令 + 亲手修出 `-O3`
- [ ] 故障 2：一句话根因 + 亲手让 `./reader` 能读到 `./model/data.txt`

## 五、自测问答

- [ ] self-test 复述题 1~4：答到答案要点的 80% 以上
- [ ] 追问链 A / B：各能连答 ≥3 层（不翻资料）
