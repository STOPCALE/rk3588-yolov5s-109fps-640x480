#!/bin/bash
# =============================================================================
#  tools/lock-freq.sh  ——  建立「可信的性能测量环境」
#
#  【为什么需要它】
#      板子上有 4 个设备的频率是**动态变化**的。任何一项没锁住，
#      测出来的性能数据就**不可比**。
#      我们曾因为没定频，得出过「三核提速 1.53×」的**假结论**，
#      并基于它差点做出「必须把模型转成 640x480」的错误决定。
#      后来又发现：连 dmc（内存控制器）都漏锁了，它让 run 慢 19%。
#
#  【用法】
#      在板子上：
#          bash  ~/myproj/tools/lock-freq.sh            # 只检查状态（不需要 root）
#          sudo bash ~/myproj/tools/lock-freq.sh lock   # 全部锁定
#
#      从 PC 上一行搞定（-t 必须有，否则 sudo 拿不到终端、输不了密码）：
#          ssh -t board "sudo bash ~/myproj/tools/lock-freq.sh lock"
#
#  【⚠️ 重启后全部失效】每次测性能前重做
#
#  【还有个 taskset】
#      锁频只保证"频率不变"，但调度器**还是可能把线程从小核挪到大核**。
#      实测：小核上 preprocess 慢 1.86 倍（0.094 -> 0.175 ms）。
#      所以跑性能测试时**必须绑大核**：
#          taskset -c 4-7 ./my_rknn_yolov5_demo ...
#      或者直接用:  bash ~/myproj/tools/run-demo.sh ...
# =============================================================================

set -u

LOCK=0
[ "${1:-}" = "lock" ] && LOCK=1

if [ "$LOCK" = "1" ] && [ "$(id -u)" != "0" ]; then
    echo "!! 锁定需要 root，请用:  sudo bash $0 lock"
    exit 1
fi

# ---- 要管的设备 ----------------------------------------------------------
#   CPU: policy0 = A55 x4 (CPU0-3)，policy4/6 = A76 x4 (CPU4-7)
CPU_POLICIES="/sys/devices/system/cpu/cpufreq/policy0
              /sys/devices/system/cpu/cpufreq/policy4
              /sys/devices/system/cpu/cpufreq/policy6"
#   devfreq: npu = 神经网络加速器，dmc = 内存控制器（最容易漏！）
DEVFREQS="/sys/class/devfreq/fdab0000.npu
          /sys/class/devfreq/dmc"

if [ "$LOCK" = "1" ]; then
    echo "== 正在锁定频率 =="
    for p in $CPU_POLICIES; do
        [ -w "$p/scaling_governor" ] && echo performance > "$p/scaling_governor"
    done
    for d in $DEVFREQS; do
        [ -w "$d/governor" ] && echo performance > "$d/governor"
    done
fi

# ---- 打印状态 ------------------------------------------------------------
echo
printf "%-24s %-14s %-14s %s\n" "设备" "governor" "cur_freq" "max_freq"
printf "%-24s %-14s %-14s %s\n" "----" "--------" "--------" "--------"

ok=1

for p in $CPU_POLICIES; do
    g=$(cat "$p/scaling_governor"   2>/dev/null)
    f=$(cat "$p/scaling_cur_freq"   2>/dev/null)
    m=$(cat "$p/scaling_max_freq"   2>/dev/null)
    printf "%-24s %-14s %-14s %s\n" "cpu $(basename $p)" "$g" "$f" "$m"
    [ "$g" != "performance" ] && ok=0
done

for d in $DEVFREQS; do
    g=$(cat "$d/governor" 2>/dev/null)
    f=$(cat "$d/cur_freq" 2>/dev/null)
    m=$(cat "$d/max_freq" 2>/dev/null)
    printf "%-24s %-14s %-14s %s\n" "$(basename $d)" "$g" "$f" "$m"
    [ "$g" != "performance" ] && ok=0
done

# 其余 devfreq（GPU 等）本项目用不到，只列出来看一眼
for d in /sys/class/devfreq/*; do
    n=$(basename "$d")
    case " fdab0000.npu dmc " in *" $n "*) continue ;; esac
    printf "%-24s %-14s %-14s %s  (用不到)\n" "$n" \
        "$(cat $d/governor 2>/dev/null)" "$(cat $d/cur_freq 2>/dev/null)" "-"
done

echo
echo "== 温度 =="
for t in /sys/class/thermal/thermal_zone*; do
    ty=$(cat "$t/type" 2>/dev/null)
    tv=$(cat "$t/temp" 2>/dev/null)
    if [ -n "$tv" ]; then
        printf "  %-20s %d.%d C\n" "$ty" "$((tv / 1000))" "$((tv % 1000))"
    fi
done

echo
if [ "$ok" = "1" ]; then
    echo "✅ 全部已定频，可以测性能了"
    echo
    echo "   跑程序记得绑大核（不然可能被调到小核，慢 2 倍）："
    echo "     taskset -c 4-7 ./my_rknn_yolov5_demo <参数...>"
    echo "   或者直接用：bash ~/myproj/tools/run-demo.sh <参数...>"
else
    echo "❌ 还有设备没定频！"
    echo "   PC 上一行搞定："
    echo "     ssh -t board \"sudo bash ~/myproj/tools/lock-freq.sh lock\""
fi
