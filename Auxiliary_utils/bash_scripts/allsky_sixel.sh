#!/bin/bash

#
# $1 - scaling factor (in percents) of the image
#

scale=50
if [ $# -gt 0 ]; then
     scale=$1
fi

unset http_proxy

clear
while [[ 1 ]]; do
    tput cup 0 0
    curl -s http://zarch.sao.ru/webcam/mirat_allsky.cgi | magick - -colors 256 +dither -normalize -resize $scale% sixel:- 
#    sleep 30s
    sleep 5s
done
