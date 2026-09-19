#!/bin/bash
# 用 val 图造一个 30fps 的测试视频（尺寸不统一，所以统一缩放+灰边补到 1280x960）
set -e
cd ~/testimg

rm -rf vidtmp
mkdir -p vidtmp
i=0
for f in val/*.jpg; do
    cp "$f" "vidtmp/$(printf "%04d" $i).jpg"
    i=$((i+1))
    if [ $i -ge 120 ]; then break; fi
done
echo "已复制 $i 张到 vidtmp/"

ffmpeg -y -loglevel error -framerate 30 -i vidtmp/%04d.jpg \
    -vf "scale=1280:960:force_original_aspect_ratio=decrease,pad=1280:960:(ow-iw)/2:(oh-ih)/2:color=gray" \
    -c:v libx264 -pix_fmt yuv420p -crf 23 \
    ~/testimg/vbtest.mp4

rm -rf vidtmp
ls -l ~/testimg/vbtest.mp4
ffprobe -v error -show_entries stream=width,height,r_frame_rate,nb_frames -of default=nw=1 ~/testimg/vbtest.mp4
