#!/bin/bash

export http_proxy=""

A="-5:00:00"
H="2:00:00"

function sendcmd(){
	echo $1 | nc localhost 10001 -q10
}

#lower limit is 0
sendcmd ":So0#"
sendcmd ":Sz${A}#"
sendcmd ":Sa${H}#"
sendcmd ":MA#"

#while true; do
#	ANS=$(sendcmd ":Gstat#")
#	echo $ANS
#	[ $ANS == "0#" ] && break
#	sleep 2
#done

