#!/usr/bin/env bash
# Regenerate sample videos for WBE wallpaper engine
set -e

ffmpeg -y -f lavfi -i "color=c=#1a1a2e:s=1920x1080:d=15,drawtext=text='WBE':fontsize=120:fontcolor=white:x=(w-text_w)/2:y=(h-text_h)/2" \
  -c:v libx264 -preset ultrafast -crf 28 \
  "$(dirname "$0")/sample-1.mp4"

ffmpeg -y -f lavfi -i "color=c=#0f3460:s=1920x1080:d=15,drawtext=text='WBE':fontsize=100:fontcolor=white:x=100+200*sin(t*2):y=100+200*cos(t*2)" \
  -f lavfi -i "color=c=#e94560:s=640x480:d=15,drawtext=text='DESKTOP':fontsize=60:fontcolor=white:x=(w-text_w)/2:y=(h-text_h)/2" \
  -filter_complex "[0:v][1:v]overlay=x=100+200*sin(t):y=100+200*cos(t):format=auto" \
  -c:v libx264 -preset ultrafast -crf 28 \
  "$(dirname "$0")/sample-2.mp4"

echo "Done"
