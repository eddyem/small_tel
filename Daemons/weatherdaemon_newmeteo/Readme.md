Weather daemon
==================

Weather daemon for new meteostation.

Open a socket at given port (default: 12345)
Parse weather data and send it to client

```
Usage: weatherdaemon [args]

        Where args are:

  -P, --pidfile=arg     pidfile name (default: /tmp/weatherdaemon.pid)
  -b, --baudrate=arg    serial terminal baudrate (default: 9600)
  -d, --device=arg      serial device name (default: none)
  -e, --emulation       emulate serial device
  -h, --help            show this help
  -l, --logfile=arg     save logs to file (default: none)
  -p, --port=arg        network port to connect (default: 12345)
  -v, --verb            logfile verbocity level (each -v increase it)
```
