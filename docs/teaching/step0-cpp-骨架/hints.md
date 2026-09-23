# step0 · 提示分级卡（卡壳时翻，不卡不看）

> 用法：① 先看 L1（方向）；还不行看 L2（检查点）；再不行看 L3（关键命令/片段）。
>
> **L3 使用规则（硬性）**：翻到 L3 ＝ 当天合上资料，凭记忆把该部分**重做一遍**，
> 并在《AI代写代码清单》（附录 E）的登记区补一行「step0 · 已翻 L3 · <日期>」。
> 为什么：提示来得太快 = 学习没发生；重做一遍才算把分数拿回来。

---

## L1 · 方向（一句话指路）

本步的完成次序 = **「先立构建系统 → 再跑通同步链路 → 最后让空壳上板运行」**：

```
CMakeLists.txt 骨架 → check.ps1 能查错 → sync.ps1 五阶段全绿 → 板上空壳程序跑起来
```

遇到「跑不起来」：按**逆序**回查——先怀疑你最后改的东西。

## L2 · 检查点（常见坑的「先查这个」）

| 症状 | 先查什么 |
|---|---|
| 配置期报版本错误 | `cmake_minimum_required` 是否 ≤ 3.16 |
| 编出来的程序莫名慢 | `flags.make` 里有没有 `-O3`（没有 → 查构建类型兜底块） |
| 程序必须 cd 才能跑 | `readelf -d` 看 RUNPATH 是不是 `$ORIGIN/lib` |
| 链接报 undefined reference | OpenCV 组件是否按「符号所在库」写全 |
| 安装后文件位置不对 | `install(DIRECTORY model DESTINATION .)` 有没有多写尾斜杠 |
| 中文乱码 + 语法报错 | `.ps1` 是不是 UTF-8 无 BOM |
| 板子连不上 | 先 `ssh board "echo OK"`；不行再用 `set-board-ip.ps1` |

## L3 · 关键命令与片段（翻到此级 = 当天合上重做 + 登记）

```powershell
# 1) 本地查错
.\tools\check.ps1
# 2) 同步（提交→推送→拉取→编译→安装）
.\tools\sync.ps1 -Message "step0: 骨架"
# 3) 板上核查三件套
ssh board "cd ~/myproj && grep -o '\-O[0-9]' build/CMakeFiles/my_rknn_yolov5_demo.dir/flags.make | sort -u"  # 期望 -O3
ssh board "readelf -d ~/myproj/install/*/my_rknn_yolov5_demo | grep -i runpath"                             # 期望 [$ORIGIN/lib]
ssh board "cd ~/myproj/install/*/ && ./my_rknn_yolov5_demo"                                                 # 空壳能启动
```

CMakeLists 的三个关键块（照着思路自己写，不要整体抄文件）：

- **构建类型兜底**：`if(NOT CMAKE_BUILD_TYPE ...) set(CMAKE_BUILD_TYPE Release CACHE STRING ... FORCE)`
- **RPATH 三连**：`set(CMAKE_SKIP_INSTALL_RPATH FALSE)` + `set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)` + `set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")`
- **安装规则**：`install(TARGETS ... RUNTIME DESTINATION .)`、`install(FILES ${RKNN_RT_LIB} ${RGA_LIB} DESTINATION lib)`、`install(DIRECTORY model DESTINATION .)`（**无尾斜杠**）
