#!/bin/bash

#
# The script creates working directory,
# copies 'run' script into it and
# edits observer name according to 
# the its argument
#
# Working directory name is formed from
# date of the script running and is computed
# as follows:
#    now - 12hours
# i.e. the day starts from 12h not from 0h!
#
# $1 - observer name
#

if [[ $# -eq 0 ]]; then
    obs_name="Fatkhullin T.A."
else
    obs_name=$1
fi

# now - 12h
let now12=`date +%s`-12*3600

# working directory
wdir=/DATA/FITS/`date -d @$now12 +%y.%m.%d`

echo -n "Creating working directory: $wdir ..."
if [[ -d $wdir ]]; then
    echo -e "\tFAILED! The directory already exists! Exit!"
    exit 1
else
    echo -e "\tOK!"
    mkdir $wdir
    cd $wdir
fi

cp ../run_full.new_foc .

# replace observer name
sed -i "0,/^OBS=\".*\"/ s//OBS=\"${obs_name}\"/" run_full.new_foc
#sed -i "0,/^OBS=\"[a-zA-Z \.]*\"/ s//OBS=\"${obs_name}\"/" run_full.new_foc
