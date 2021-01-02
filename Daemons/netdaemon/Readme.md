Network daemon snippet
==================

Open a socket at given port (default: 4444), works with http & direct requests.
Can read and send commands over serial interface.

Pieces with user code marked as 'INSERT CODE HERE'.


Usage: netdaemon [args]

        Where args are:

  -b, --baudrate=arg   serial terminal baudrate (default: 115200)
  -e, --echo           echo users commands back
  -h, --help           show this help
  -i, --device=arg     serial device name (default: none)
  -l, --logfile=arg    save logs to file (default: none)
  -p, --port=arg       network port to connect (default: 4444)
  -t, --terminal       run as terminal
  -v, --verb           logfile verbocity level (each -v increase it)
