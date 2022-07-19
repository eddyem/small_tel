#!/bin/bash

A="$1" # azimuth in sexagesimal notation, e.g. 10:00:00
H="$2" # altitude, same format as form azimuth

function sendcmd(){
	echo $1 | nc localhost 10001 -q10
}

sendcmd ":Sz${A}#"
sendcmd ":Sa${H}#"
# not slew
sendcmd ":MA#"
# slew
# sendcmd ":MS#"
while true; do
	ANS=$(sendcmd ":Gstat#")
	echo $ANS
	[ $ANS == "7#" ] && break
	sleep 2
done
