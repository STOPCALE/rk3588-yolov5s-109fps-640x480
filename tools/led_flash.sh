#!/bin/bash
# =============================================================================
# tools/led_flash.sh —— LED 闪烁 + 墙钟记录（光参照延迟实验用）
#
# 用法（板子上）：bash led_flash.sh [csv路径] [周期数] [亮ms] [灭ms]
#   例：bash led_flash.sh /tmp/lat2/led.csv 10 600 600
#
# 说明：
#   - 用 bash 内建 $EPOCHREALTIME 取时（无 fork/date 子进程开销），
#     对每次开关记录 [写前, 写后] 两个时刻 → 发光时刻落在该区间内（<1ms 宽）。
#   - 写 sysfs 的 echo 是 bash 内建（含重定向下也无 fork），开销 ~0.1~0.5ms。
#   - CSV 格式：seq,state,before_s,after_s   （state: 1=亮, 0=灭；时间为墙钟秒）
#   - 运行结束自动灭灯。
# =============================================================================
set -u

led=/sys/class/leds/status_led/brightness
out=${1:-/tmp/lat2/led.csv}
n=${2:-10}
onms=${3:-600}
offms=${4:-600}

mkdir -p "$(dirname "$out")"
echo "seq,state,before_s,after_s" > "$out"

seq_n=0
for i in $(seq 1 "$n"); do
    seq_n=$((seq_n + 1))
    b=$EPOCHREALTIME; echo 255 > "$led"; a=$EPOCHREALTIME
    echo "$seq_n,1,$b,$a" >> "$out"

    sleep "$(awk -v m="$onms" 'BEGIN{print m/1000}')"

    seq_n=$((seq_n + 1))
    b=$EPOCHREALTIME; echo 0 > "$led"; a=$EPOCHREALTIME
    echo "$seq_n,0,$b,$a" >> "$out"

    sleep "$(awk -v m="$offms" 'BEGIN{print m/1000}')"
done

echo 0 > "$led"
echo "led_flash done: $out"
