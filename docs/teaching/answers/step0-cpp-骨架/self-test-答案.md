# step0 · 自测答案（对照 `step0-cpp-骨架/self-test.md`）

> 复述题给"要点"；变式题给"参考答案"；**追问链不给答案**（面试训练，卡壳回 concept-map）。

## 一、复述题要点

**1. 日常 4 命令**（cwd 必须是 `E:\desk\学习\C++\mytest\new`）：

| # | 命令 | 作用 |
|---|---|---|
| 1 | `.\tools\check.ps1` | 本地 1 秒语法查错 |
| 2 | `.\tools\sync.ps1 -Message "..."` | 提交+推送+板子拉取+编译+安装（五合一） |
| 3 | `ssh board "bash ~/myproj/tools/run-demo.sh ..."` | 在板子上跑程序看结果 |
| 4 | `.\tools\set-board-ip.ps1 <IP>` | 板子 IP 变了（DHCP） |

**2. Git 三仓库**：`PC 真源（一切操作）→ push → 板子裸仓库 ~/myproj.git（只收 push）→ pull --ff-only → 板子工作副本（只读，禁止 commit/add/编辑）`。

**3. CMake 四阶段**：Configure → Generate → Build → Install；
`cmake -B build -S .` = 前两步；**改了 `CMakeLists.txt` 必须重跑 configure+generate**（尤其改 `install()` 规则——`cmake_install.cmake` 是 generate 阶段生成的）。

**4. 六条不可动约定**（≥4 即可）：① `cmake_minimum_required` ≤3.16；② RPATH 必须 `$ORIGIN/lib`；③ `install(DIRECTORY model ...)` 无尾斜杠；④ `CMAKE_BUILD_TYPE` 必须有默认值；⑤ OpenCV 组件按"符号所在库"选；⑥ 关机必须 `sync && poweroff` 并等灯灭。

## 二、变式题参考答案

**变式 1（IP 变化）**：
① 症状：`ssh board` 超时/解析不到 → ② 在板子屏幕上 `ip addr` 或路由器后台查新 IP → ③ PC 执行 `.\tools\set-board-ip.ps1 <新IP>`（改的是 `~/.ssh/config` 里 `Host board` 的 `HostName`）→ ④ 验证 `ssh board "echo OK"` → ⑤ 再跑一次 `sync.ps1` 确认全链路。

**变式 2（新板子 8 步）**：
① 装系统基础依赖（cmake/gcc/g++/make/libopencv-dev）→ ② 装运行时依赖（`gstreamer1.0-libav` 等）→ ③ PC 配公钥免密 + `.ssh/config` 别名 → ④ 板子建裸仓库 + 工作副本 → ⑤ PC `git remote add board ...` + 首次 `push -f` → ⑥ 板子重新 clone 对齐 → ⑦ 拷贝 `tools/lock-freq.sh`、`run-demo.sh` 验证 → ⑧ 配 `~/bin/demo` 入口（符号链接 + PATH）。

**变式 3（本地过了、板上炸）**：
- `.c` 文件本地查不了（MinGW 无 POSIX 头）；
- 链接问题（本地不链接）；
- 模板「写了但没实例化」（案例 17）；
- 本地头文件与板上 OpenCV 版本不一致（本地是拷来的头，板上是 4.2.0）。
