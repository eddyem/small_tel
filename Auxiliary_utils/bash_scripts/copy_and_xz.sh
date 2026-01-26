#!/bin/bash

#
# $1 - directory to be copied
#

RDIR="/home/obs/robotel1_2025"

# copy non-FITS files
tar c --exclude='*.fit' --exclude='*shit*' $1 | ssh obs@roboserv "tar x -C  $RDIR"
#tar c --exclude='*.fit' --exclude='*shit*' $1 | ssh obs@roboserv "tar x -C  /home/obs/robotel1_2023"

# copy FITS files and XZ-ing it on remote server
CMD='sh -c "xz -6e -T0 - > $TAR_FILENAME.xz"'
tar c $1/*.fit | ssh obs@roboserv "cd  $RDIR; tar x --to-command='$CMD'"
#tar c $1/*.fit | ssh obs@roboserv "cd  /home/obs/robotel1_2023; tar x --to-command='$CMD'"

ssh obs@roboserv "cd $RDIR;  tar c $1 | ssh data@robostorage 'tar x -C  /mnt/ARCHIVE/ROBOTEL1/'"
