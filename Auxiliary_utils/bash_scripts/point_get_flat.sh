#!/bin/bash

#A="00:00:00"
#H="85:00:00"

A="$1" # azimuth in sexagesimal notation, e.g. 10:00:00
H="$2" # altitude, same format as form azimuth

filename="$3" # basename for result file (see fli_control -h)
exptime="$4"  # exposure time in milliseconds

function sendcmd(){
	echo $1 | nc localhost 10001 -q10
}

preflash > /dev/null &

#for x in $(seq 1 10); do
sendcmd ":Sz${A}#"
sendcmd ":Sa${H}#"
sendcmd ":MS#"
while true; do
	ANS=$(sendcmd ":Gstat#")
	echo $ANS
	[ $ANS == "0#" ] && break
	sleep 2
done
#sendcmd ":FLIP#"

sleep 1s
echo "Waiting for preflash finishing ..."
wait 

#sleep 5
#./preflash
#/usr/bin/fli_control -r /tmp/10micron.fitsheader -x $2 -O sunsky -Y flat $1
/usr/bin/fli_control -r /tmp/10micron.fitsheader -x ${exptime} -O sunsky -Y flat ${filename}
#done

# to prevent "bad" image after the exposition?
fli_control -v32 -h32 -x1 -d
