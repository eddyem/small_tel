Some examples of usage of libsidservo
=====================================

## Auxiliary files

*conf.c*, *conf.h* - base configuration - read from file (default: servo.conf) - to simplify examples running when config changes

*dump.c*, *dump.h* - base logging and dumping functions, also some useful functions like get current position and move to zero if current position isn't at zero.

*traectories.c*, *traectories.h* - modeling simple moving object traectories; also some functions like get current position in encoders' angles setting to zero at motors' zero.

*simpleconv.h*


## Examples

*dumpmoving.c* (`dump`) - dump moving relative starting point by simplest text commands "X" and "Y".

*dumpmoving_scmd.c* (`dump_s`) - moving relative starting point using "short" binary command.

*dumpswing.c* (`dumpswing`) - shake telescope around starting point by one of axis.

*goto.c* (`goto`) - get current coordinates or go to given (by simplest "X/Y" commands).

*scmd_traectory.c* (`traectory_s`) - try to move around given traectory using "short" binary commands.

*SSIIconf.c* (`SSIIconf`) - read/write hardware configuration of controller

