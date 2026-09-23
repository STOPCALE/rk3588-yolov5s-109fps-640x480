# step0 · 故障注入答案（对照 `step0-cpp-骨架/fault-injection.md`）

> 先自己修完再看。答案给「根因 + 参考排查路径 + 修复 + 验证」。

---

## 故障 1 · 运行变慢

**根因（一句话）**：`CMakeLists.txt` 里**没有构建类型兜底**——单配置生成器下 `CMAKE_BUILD_TYPE` 为空，
所有 `CMAKE_CXX_FLAGS_<CONFIG>` 全部失效，编译器收到的就是 **-O0（无优化）**。

**参考排查路径**：
1. 时间差好几倍 → 不是算法问题（结果正确），先怀疑"编译出来的东西不一样"。
2. 看编译标志：`grep -o '\-O[0-9]' build/CMakeFiles/work.dir/flags.make | sort -u`
   → **什么都不输出**（没有 -O 等级）= 关键证据。
3. 对照实验：重新构建时显式加 `-DCMAKE_BUILD_TYPE=Release` → flags.make 出现 `-O3`，elapsed 显著下降。

**修复（参考实现同款）**：在 `CMakeLists.txt` 的 `project()` 之后加：

```cmake
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()
```

**验证**：
```bash
cmake -B build -S . && cmake --build build -j4
grep -o '\-O[0-9]' build/CMakeFiles/work.dir/flags.make | sort -u   # 期望：-O3
./build/work                                                        # 期望：elapsed 显著变小
```

**深挖**：见 `docs/02-构建与环境配置.md` §A.4 坑 1；这正是主工程 CMake 改进清单的第 0 项。

---

## 故障 2 · 文件打不开

**根因（一句话）**：`install(DIRECTORY model/ DESTINATION .)` 的 **结尾斜杠**——
「有斜杠 = 拷贝**目录的内容**，无斜杠 = 拷贝**目录本身**」。
加了斜杠后 `model/` 这一层没了，`data.txt` 被直接铺进安装根目录 → 运行时 `./model/data.txt` 自然找不到。

**参考排查路径**：
1. `cd install && ./reader` 报 fail → 确认程序没错，是**环境缺文件**。
2. `ls -la install/` → 根目录里躺着一个孤零零的 `data.txt`；没有 `model/` 目录。
3. `ls 源码目录/model/` → 明明有。对比"源码 vs 安装后"的目录结构 → 差异出现在**安装规则**。
4. 回去看 `CMakeLists.txt` 的 install 段 → 发现尾斜杠。

**修复**：去掉尾斜杠：

```cmake
install(DIRECTORY model DESTINATION .)   # ← 去掉 model 后面的 "/"
```

**验证（注意顺序）**：
```bash
cmake -B build -S .        # ★ 改了 install 规则必须重跑 configure（install 脚本是 generate 阶段生成的）
cmake --build build -j4
cmake --install build
cd install && ./reader     # 期望输出：data: hello from model dir
```

**为什么这个 bug 在真实工程里"致命且隐蔽"**：主工程里模型目录下是
`coco_80_labels_list.txt` 这类**被代码用相对路径硬编码读取**的文件（02 坑 4）。
一旦丢层，程序**不崩溃**，只是"什么都检测不到/结果全空"，极难排查。
所以这条安装规则被列为**不可动约定**之一。
