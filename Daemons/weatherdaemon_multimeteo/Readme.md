Weather daemon for several different weather stations
=====================================================

## Usage:

```
Usage: weatherdaemon [args]
    Be careful: command line options have priority over config
    Where args are:

  -P, --pidfile=arg    pidfile name (default: /tmp/weatherdaemon.pid)
  -c, --conffile=arg   configuration file name (consists all or a part of long-named parameters and their values (e.g. plugin=liboldweather.so)
  -h, --help           show this help
  -l, --logfile=arg    save logs to file (default: none)
  -p, --plugin=arg     add this weather plugin (may be a lot of) (can occur multiple times)
  -v, --verb           logfile verbocity level (each -v increased)
  --port=arg           network port to connect (default: 12345); hint: use "localhost:port" to make local net socket
  --sockpath=arg       UNIX socket path (starting from '\0' for anonimous) of command socket
```


TODO: brief documentation will be here
