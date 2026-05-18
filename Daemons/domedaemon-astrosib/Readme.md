# domedaemon‑astrosib

**domedaemon‑astrosib** is a daemon that controls an [**Astrosib**](https://astrosib.ru/) all‑sky
dome via a serial line. It provides a TCP/UNIX socket interface for clients, integrates with a
weather proxy to enforce safety rules, and generates a FITS‑style header file with current dome
status.

---

## Features

- **Dome open/close** – fully open or close the dome  
- **Stop movement** – immediately stop any ongoing dome motion  
- **Independent cover control** – open or close only the northern or southern cover  
- **Relay control** – switch three auxiliary relays (power, light, etc.)  
- **Status reporting** – cover states, encoder angles, relay states, rain sensor, motor currents,
temperatures
- **Weather integration** – automatically closes the dome and prevents opening when weather is bad
or data is stale
- **Manual override** – via `SIGUSR1` (forbid) / `SIGUSR2` (allow) signals
- **FITS header file** – writes current dome state into a FITS‑style header file (e.g., for later
use by image processing pipelines)
- **Network interface** – supports both INET (TCP) and UNIX domain sockets  
- **Daemon mode** – forks, writes PID file, restarts child on crash  

---

## Command‑line options

| Option               | Argument          | Default                           | Description                                                       |
|----------------------|-------------------|-----------------------------------|-------------------------------------------------------------------|
| `-h`, `--help`       | –                 | –                                 | Show help                                                         |
| `-d`, `--device`     | *path*            | –                                 | Full path to serial device (e.g. `/dev/ttyUSB0`)                  |
| `-n`, `--node`       | *string*          | `55555`                           | Node specification: port number for TCP, or path for UNIX socket  |
| `-l`, `--logfile`    | *path*            | –                                 | Full path to log file                                             |
| `-u`, `--unix`       | –                 | 0                                 | Use UNIX socket instead of TCP                                    |
| `-v`, `--verbose`    | –                 | 0                                 | Increase verbosity (each `-v` adds 1 level)                       |
| `-p`, `--pidfile`    | *path*            | `/tmp/domedaemon.pid`             | PID file path                                                     |
| `-H`, `--headerfile` | *path*            | `/tmp/dome.fits`                  | Output file for FITS‑style header                                 |
| `-N`, `--domename`   | *name*            | `Astrosib`                        | Dome name inserted into the header                                |

---

## Socket protocol

The daemon listens for text‑based commands on the configured socket. Each command returns one or
more lines of output, terminated by newline (`\n`).

### Commands

| Command                         | Description                                                                | Example                                      |
|---------------------------------|----------------------------------------------------------------------------|----------------------------------------------|
| `unixt`                         | Return server’s current UNIX time (seconds with two decimal places)        | `unixt` → `unixt=1704067200.12`              |
| `status`                        | Get dome status in old format (four numbers)                               | `status` → `status=3,3,0,0`                  |
| `statust`                       | Get dome status in human‑readable format                                   | `statust` → (see example below)              |
| `relay1` … `relay3`             | Get/set relay state (0 = off, 1 = on)                                      | `relay1` → `relay1=1` (if queried without argument) |
| `open`                          | Fully open dome (only if weather permits and observations not forbidden)   | `open`                                       |
| `close`                         | Fully close dome                                                           | `close`                                      |
| `stop`                          | Immediately stop dome movement                                             | `stop`                                       |
| `half1` … `half2`               | Get/set state of a single cover (0 = closed, 1 = opened)                   | `half1 0`                                    |

Each setter returns an `OK` answer in case of success or error code like `BADVAL` for bad value of
setter.

### Example `statust` response

```
status=3,3,0,0
operations=permitted
cover1=closed
cover2=closed
angle1=0
angle2=0
relay1=0
relay2=0
relay3=0
reqtime=1779087731.049449921
```

- `operations` can be `permitted` or `forbidden` (manual override).
- `cover1` / `cover2` report `closed`, `opened` or `intermediate`.
- `angle1` / `angle2` are encoder values (degrees).
- `relay1` … `relay3` show the current state of each relay.
- `reqtime` is the time when the status was last read from the hardware.

---

## Weather integration

The daemon retrieves weather data via the **weather_proxy** shared memory interface (`get_weather_data()`).  
Safety rules:

- If `weather.forceoff`, `weather.rain`, or `weather.weather > WEATHER_BAD` → dome is closed and opening is blocked.
- If the weather data is older than **300 seconds** (`WEATHER_LOST`) → treated as lost communication → dome is closed and blocked.
- When weather becomes good again, the daemon automatically resets the `BadWeather` flag and allows normal operation.

The weather is polled every **5 seconds** (`WEATH_POLL`).

---

## Signal handling

| Signal   | Effect (in child process)                           |
|----------|------------------------------------------------------|
| `SIGUSR1`| Forbid observations – close dome and block open     |
| `SIGUSR2`| Allow observations – remove manual block            |
| `SIGTERM`| Stop the daemon and remove PID file                 |
| `SIGINT` | Same as `SIGTERM`                                   |
| `SIGHUP` | Same as `SIGTERM`                                   |

---

## FITS header file

The daemon writes a text file (default `/tmp/dome.fits`) with **FITS‑like** keyword/value pairs.  
The file is updated every polling cycle (every 5 seconds) and can be used by external pipelines to record dome state.

Example content:

```
DOME    = 'Astrosib'            / Dome manufacturer/name
DOMESTAT= 'idle'                / Dome status
DOMECVR1= 'closed'              / Dome cover 1 status
DOMECVR2= 'closed'              / Dome cover 2 status
DOMEANG1= 0                     / Dome cover 1 angle
DOMEANG2= 0                     / Dome cover 2 angle
DOMERLY1= 0                     / Dome relay1 state
DOMERLY2= 0                     / Dome relay2 state
DOMERLY3= 0                     / Dome relay3 state
TDOMMEAS= 1779090474.704        / Measurement time: 2026-05-18 10:47:54
```

The file is created atomically using `mkstemp()` + `rename()`.

---

## Building and dependencies

### Dependencies

- [libusefull_macros](https://github.com/eddyem/snippets_library) – common macros and helpers
- [libweather_proxy](https://github.com/eddyem/small_tel/tree/master/Daemons/weather_proxy) – shared memory weather data
- `pthread`, `libc`

### Build

```bash
git clone --depth=1 https://github.com/eddyem/small_tel.git
cd small_tel/Daemons/domedaemon-astrosib
mkdir build
cd build
cmake ..
make
```

The resulting binary is `domedaemon`.

### Installation

Copy the binary to a suitable location (e.g. `/usr/local/bin`) or run `sudo make install`.
It is recommended to run the daemon as a system service (using init script, e.g. in `/etc/local.d/`)
so that it starts automatically at boot.

---

## Usage example

Start the daemon with a UNIX socket and weather‑aware header:

```bash
domedaemon -vv -d /dev/ttyS3 -l /var/log/telescope/domedaemon.log
```

Connect to the socket using `socat`, `nc` or my [`tty_term`](https://github.com/eddyem/tty_term):

```bash
nc localhost 55555
> statust
```

Send `SIGUSR1` to forbid observations (e.g. before maintenance):

```bash
killall -USR1 domedaemon
```

---

## Notes

- The serial protocol is specific to Astrosib hardware (commands like `STATUS`, `OPENDOME`,
`CLOSEDOME`, `STOPDOME`, `SHUTTERMOVEDEG`, `SWITCHTOGGILE` are hard‑coded).
- Encoder values are reported as raw integers; interpretation depends on the physical setup.
- The daemon polls the dome every **5 seconds** (`T_INTERVAL`). During that interval it also
processes client requests and checks weather.
- If the serial device becomes unresponsive, the status will return an error and the header will
not be updated.

---

## License

**GNU General Public License v3.0** or later.
See the `LICENSE` file in repository's root directory or <http://www.gnu.org/licenses/> for details.
