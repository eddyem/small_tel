#!/bin/bash

#
# $1 - scaling factor (in percents) of the image
#

function get_val {
  echo $(echo $1 | cut -d "=" -f 2 | cut -d "." -f 1)
}

white_col="\e[97m"
red_col="\e[1m\e[31m"
end_col="\e[0m"
last_row=0

scale=50
if [ $# -gt 0 ]; then
     scale=$1
fi

unset http_proxy

im_sleep=20 # in secs
info_sleep=180 # in secs

n_info=$((info_sleep/im_sleep))



clear
while [[ 1 ]]; do
    # weather info
    m_old=($(curl 192.168.70.33:12345 2>/dev/null)// / )
    rain=${m_old[0]}
    clouds=${m_old[1]}
    temp=${m_old[2]}

    m_new=($(curl localhost:3333/stat3600 2>/dev/null | sed 's/[\x01-\x1F\x7F]//g')// / )
    windmax=${m_new[0]}
    m_new=($(curl localhost:3333 2>/dev/null | sed 's/[\x01-\x1F\x7F]//g')// / )
    wind=${m_new[0]}
    humi=${m_new[4]}

    rain_col=$white_col
    clouds_col=$white_col
    humi_col=$white_col
    wind_col=$white_col
    wind_max_col=$white_col

    let rain_flag=$(get_val $rain)
    if [[ $rain_flag -eq 1 ]]; then
        rain_col=$red_col
    fi

    let clouds_val=$(get_val $clouds)
    if [[ $clouds_val -le 1500 ]]; then
        clouds_col=$red_col

    fi

    let humi_val=$(get_val $humi)
    if [[ $humi_val -ge 90 ]]; then
        humi_col=$red_col
    fi

    let wind_val=$(get_val $wind)
    if [[ $wind_val -ge 10 ]]; then
        wind_col=$red_col
    fi

    let wind_max_val=$(get_val $windmax)
    if [[ $wind_max_val -ge 10 ]]; then
        wind_max_col=$red_col
    fi

    ncols=`tput cols`
    start_col=$((ncols-25))

    tput cup $last_row $start_col
#    echo -e "\e[4m`date`:\e[0m"
    echo -e "\e[4m`date +'%F %T'`:\e[0m"

    ((++last_row))

    tput cup $last_row $start_col
    echo -e "$clouds_col$clouds $rain_col($rain)$end_col"

    ((++last_row))

    tput cup $last_row $start_col
#    echo -e "$temp $humi_col($humi)$end_col"
    temp_val=`printf "%.1f" ${temp#*=}`
    echo -e "Temp=$temp_val, ${humi_col}Hum=$humi_val%$end_col"

    ((++last_row))

    tput cup $last_row $start_col
#    echo -e "$wind_col$wind $wind_max_col($windmax @hour)$end_col\n"
    wind_max_val=`printf "%.1f" ${windmax#*=}`
    echo -e "$wind_col$wind $wind_max_col(Max=$wind_max_val/hour)$end_col\n"

    IFS='[;' read -p $'\e[6n' -d R -rs _ last_row col _

    # allsky image

    for i in `seq $n_info`; do
        tput cup 0 0
        curl -s http://zarch.sao.ru/webcam/omea_allsky.cgi  | magick - -colors 256 -normalize +dither -resize $scale% sixel:- 
#        curl -s http://zarch.sao.ru/webcam/mirat_allsky.cgi | magick - -colors 256 -resize $scale% sixel:- 
        sleep ${im_sleep}s
    done


done
