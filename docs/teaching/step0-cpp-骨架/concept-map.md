# step0 · 概念地图 —— C++ 工程骨架（CMake / 工具链 / Git 工作流）

> **本步目标**：从空目录出发，搭出「能编译、能一键同步、能上板运行空壳」的工程骨架。
> **参考实现**（对照对象）：`CMakeLists.txt`、`tools/check.ps1`、`tools/sync.ps1`、`tools/set-board-ip.ps1`、
> `src/main.cc`（只看「入口与参数」部分）、目录结构约定。
> **配套章节**：01（全貌）/ 02（构建与环境）/ 附录 A（日常命令，操作速查）。
> **用法**：自己先写 → 卡壳翻 `hints.md` → 做完对 `acceptance.md`；本文件用来「对答案 + 补概念」。

---

## 一、逐块讲解

### 1.1 `CMakeLists.txt`（12 块 · 每块一行「为什么」）

| # | 定位（Ctrl+F 搜索串） | 做什么 | 为什么（错了会怎样） |
|---|---|---|---|
| 1 | `cmake_minimum_required` | 最低版本 3.16 | 板上 apt 的 cmake 只有 **3.16.3**；写更高 → 板上 configure 直接失败 |
| 2 | `project(... LANGUAGES C CXX)` | 启用 C + C++ | 有 `src/uart.c`；不写 C → .c 文件无法编译 |
| 3 | `CMAKE_CXX_STANDARD 14` | 语言标准 | `REQUIRED ON` 不支持就报错（不静默降级）；`EXTENSIONS OFF` 用 `-std=c++14` 而非 `gnu++14` |
| 4 | `CMAKE_BUILD_TYPE` 默认值 | 缺省 = Release | ⚠️ 单配置生成器默认为空 → `*_FLAGS_RELEASE` 全部失效 → 编出 **-O0**（02 坑 1） |
| 5 | `CMAKE_EXPORT_COMPILE_COMMANDS ON` | 生成 compile_commands.json | IDE / clangd 跳转用 |
| 6 | `CMAKE_INSTALL_PREFIX` + `CMAKE_INSTALL_RPATH "$ORIGIN/lib"` | 自包含部署 | `$ORIGIN`＝可执行文件自身目录 → 安装包**拷哪都能跑**（02 坑 2） |
| 7 | `RKNN_RT_LIB` / `RGA_PATH` | 第三方库路径 | RGA 路径用 `${CMAKE_SYSTEM_PROCESSOR}`（不硬编码 aarch64） |
| 8 | `find_package(OpenCV 4 REQUIRED COMPONENTS ...)` | OpenCV 依赖 | 组件按「符号在哪个 .so」写全（02 坑 3：`highgui.hpp` 会间接引 `videoio`，只链 highgui 会「编译过、链接炸」） |
| 9 | `find_package(Threads REQUIRED)` | 线程库 | 用导入目标 `Threads::Threads`，自动带 `-pthread`（编译+链接都要） |
| 10 | `add_executable(my_rknn_yolov5_demo ...)` | 目标 + 源文件清单 | 清单里的文件**必须存在**；还没写的先注释掉（02 常见报错表） |
| 11 | `target_include_directories` / `target_link_libraries` / `target_link_options` | 目标级配置 | 现代 CMake 铁律：**目标级作用域**；且必须写在 `add_executable` 之后（02 §A.2） |
| 12 | `install(TARGETS/FILES/DIRECTORY ...)` | 安装规则 | `.so` 用 FILES（644 权限）；`model` **末尾不加斜杠**（02 坑 4：加了丢 `model/` 层 → 运行期读不到标签） |

> 六条「不可动约定」的完整推导 + 验证命令在 **02 §A.4**；日常操作在 **附录 A 第 0 章**。

### 1.2 `tools/check.ps1` —— 1 秒本地语法查错

| 定位（搜索串） | 做什么 |
|---|---|
| g++ 路径（`E:\mingw64`） | 调 MinGW g++ 做 `-fsyntax-only`（只解析，不链接、不产出） |
| 文件清单 | 覆盖 `src/*.cc`；**`.c` 文件查不了**（MinGW 没有 `<termios.h>` 等 POSIX 头） |
| OpenCV `-isystem` | 指向仓库外的本地头文件（供 IntelliSense，不进 Git） |

**能抓**：拼写错误、少分号、漏 `return`、明显的类型告警。**抓不了**：链接错误；模板「写了但没实例化」的问题（见《方法论》案例 17）。

### 1.3 `tools/sync.ps1` —— 一条命令的 5 个阶段

| 阶段 | 实际动作 | 成功标志 |
|---|---|---|
| ① 提交 | PC：`git add -A` + `git commit` | `[OK] 已提交` |
| ② 推送 | PC：`git push board master` | `[OK] 已推送` |
| ③ 拉取 | 板子：`git pull --ff-only` | `Fast-forward` / `Already up to date` |
| ④ 编译 | 板子：`cmake -B build -S .` + `--build -j8` | `[OK] 编译成功` |
| ⑤ 安装 | 板子：`cmake --install build` | `[OK] 全部完成 🎉` |

**脚本语言要点**：外部命令失败要能感知——`$LASTEXITCODE -ne 0` 时中断；否则「失败了还继续跑」，最后的红字和真正原因对不上（02 §D.1）。

### 1.4 目录结构与「唯一真源」约定

- PC（`E:\desk\学习\C++\mytest\new`）= 唯一编辑/提交地；板子 `~/myproj` **只读**（只允许 `git pull --ff-only`）
- 板子 `~/myproj.git` = 裸仓库中转站；`ssh board` 免密别名
- `.vscode/settings.json` 强制 UTF-8 + LF 行尾

**为什么不用 scp 传源码**：曾出现「改了 3 个文件只传 1 个」「后传的覆盖先传的」「没历史无法回退」三类事故；Git 的 commit 历史就是时间机器（附录 A §0.5）。

---

## 二、概念清单（先查这里，不懂再搜）

| 概念 | 一句话定义 | 搜索关键词 | 推荐资料 |
|---|---|---|---|
| CMake 四阶段 | configure→generate→build→install；改 CMakeLists 需重跑前两步 | `cmake configure generate build install` | 02 §A.1；CMake 官方 Tutorial（priority.1） |
| 目标级作用域 | `target_*` 只影响指定目标；目录级＝全局污染 | `target_include_directories vs include_directories` | 02 §A.2 |
| RPATH / RUNPATH | 动态库搜索路径写进 ELF；`$ORIGIN`＝可执行文件自身目录 | `rpath origin runpath readelf` | 02 坑 2；CSAPP 第 7 章（链接） |
| -O0 / -O3 | 编译优化等级；由 `CMAKE_BUILD_TYPE` 决定 | `CMAKE_BUILD_TYPE empty O0` | 02 坑 1 |
| install 语义 | FILES=644、PROGRAMS=755、DIRECTORY 尾斜杠=拷「内容」 | `cmake install DIRECTORY trailing slash` | 02 坑 4/5/6 |
| SSH 公钥认证 | 私钥本地签名，服务器只存公钥 | `ssh public key challenge signature` | 02 §B.1 |
| Host=别名 | `.ssh/config` 里 Host 是别名，HostName 才是地址 | `ssh config Host vs HostName` | 02 §B.2 |
| Git 三仓库 | PC 真源 → 裸仓库 → 板子只读副本 | `git bare repo pull --ff-only` | 02 Part C |
| UTF-8 BOM | PS 5.1 把无 BOM 脚本按 GBK 读 → 中文乱码 | `powershell 5.1 BOM UTF8 GBK script` | 02 §D.2 |
| $LASTEXITCODE | 上一条**外部命令**的退出码；脚本必查 | `powershell LASTEXITCODE` | 02 §D.1 |
| MinGW 语法检查 | 本地 `-fsyntax-only`，只解析不产出 | `-fsyntax-only` | 附录 A §0.2 |
| ELF 排查三件套 | `readelf -d` / `ldd` / `file` | `readelf dynamic section` | 02 §A.5 |

---

## 三、逐行预检表（自己打勾；❓＝还没把握）

> 用法：重写（或写完）后逐块过；能自己讲清「做什么 / 为什么」打 ✅，否则 ❓ 回上一节找资料。

### 3.1 `CMakeLists.txt`

| # | 块 | 一句话描述 | ✅/❓ |
|---|---|---|---|
| 1 | 版本与工程 | 最低版本 + 工程名 + C/CXX 两种语言 | |
| 2 | 语言标准 | C++14 / required / no extensions | |
| 3 | 构建类型兜底 | 为空则 Release（防 -O0） | |
| 4 | 自包含安装 | prefix + 三条 RPATH 设置 | |
| 5 | 第三方库变量 | RKNN / RGA 路径（用架构变量） | |
| 6 | find_package | OpenCV 组件 + Threads 导入目标 | |
| 7 | 目标定义 | add_executable + 源文件清单 | |
| 8 | 目标级配置 | include / link / link_options | |
| 9 | 安装规则 | TARGETS / FILES / DIRECTORY（model 无斜杠） | |

### 3.2 `tools/check.ps1` / `tools/sync.ps1`

| # | 块 | 一句话描述 | ✅/❓ |
|---|---|---|---|
| 1 | check：编译器路径 | MinGW g++、`-fsyntax-only` | |
| 2 | check：文件集合 | 只查 .cc；为什么查不了 .c？ | |
| 3 | sync：提交 | add → commit；失败时看哪里 | |
| 4 | sync：推送 / 拉取 | push 裸仓库；板子 `pull --ff-only` | |
| 5 | sync：编译安装 | configure → build → install 三段 | |
| 6 | sync：错误中断 | `$LASTEXITCODE` 检查点在哪 | |

### 3.3 `src/main.cc`（入口，只看文件头与参数解析部分）

| # | 块 | 一句话描述 | ✅/❓ |
|---|---|---|---|
| 1 | 参数约定 | `<模型> <输入> <帧数> [flags]` 的解析顺序 | |
| 2 | 启动打印 | 版本 / 张量属性打印的作用 | |
| 3 | 输入判定 | 图片 / 视频 / 摄像头三条分支如何区分 | |
