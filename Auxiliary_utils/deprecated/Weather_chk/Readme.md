Check meteostation parameters
===========================

    Usage: chkweather [args]
    
            Where args are:
    
      -d, --devname=arg   serial device name
      -h, --help          show this help
      -r, --raw           show raw information from meteostation
      -s, --speed=arg     baudrate (default: 9600)


Output: 
Rain=0/1
Clouds=xxxx

Return value:
0 if no rain and Clouds>1800, 1 if there's rainy or Clouds>1800
