# teldaemon_astrosib

**teldaemon_astrosib** is a daemon that controls an [**Astrosib**](https://astrosib.ru/) telescope
equipment via a serial line or virtual serial port over TCP. It provides a TCP/UNIX socket
interface for clients, integrates with a
[`weather_proxy`](https://github.com/eddyem/small_tel/tree/master/Daemons/weather_proxy) to enforce
safety rules, and generates a FITS‑header file with current telescope status.

---

## Features

- **Shutter control** – open / close
- **Focuser control** – absolute move, relative move, stop
- **Cooler control** – switch primary mirror cooler on/off
- **Heater control** – switch secondary mirror heater on/off
- **Status reporting** – shutters state, focuser position, cooler/heater status, mirror and ambient
temperatures
- **Weather integration** – automatically closes shutters and prevents opening when weather is bad
or data is stale
- **Manual override** – via `SIGUSR1` (forbid) / `SIGUSR2` (allow) signals
- **FITS header file** – writes current telescope state into a FITS‑style header file (e.g., for
later use by image processing pipelines)
- **Network interface** – supports both INET (TCP) and UNIX domain sockets
- **Daemon mode** – forks, writes PID file, restarts child on crash

---

## Command‑line options

| Option               | Argument          | Default                     | Description                                                                 |
|----------------------|-------------------|-----------------------------|-----------------------------------------------------------------------------|
| `-h`, `--help`       | –                 | –                           | Show help                                                                   |
| `-v`, `--verbose`    | –                 | 0                           | Increase verbosity (each `-v` adds 1 level)                                |
| `-l`, `--logfile`    | *path*            | –                           | Full path to log file (otherwise stderr)                                   |
| `-n`, `--node`       | *string*          | –                           | Node specification: IP, `name:IP`, or path for UNIX socket. For anonymous UNIX socket use `\0path` or `@path`. |
| `-u`, `--unixsock`   | –                 | 0                           | Use UNIX socket instead of INET                                            |
| `-m`, `--maxclients` | *number*          | 2                           | Maximum number of simultaneously connected clients                         |
| `-p`, `--pidfile`    | *path*            | `/tmp/teldaemon.pid`        | PID file path                                                              |
| `-d`, `--serialdev`  | *path*            | –                           | Full path to serial device (e.g. `/dev/ttyUSB0`)                           |
| `-b`, `--baudrate`   | *speed*           | 9600                        | Serial baud rate                                                           |
| `-t`, `--sertmout`   | *microseconds*    | 1000000 (1 sec)             | Serial read timeout (µs)                                                   |
| `-H`, `--headerfile` | *path*            | `/tmp/telescope.fits`       | Output file for FITS‑style header                                          |
| `-N`, `--telname`    | *name*            | `Astro-M (1)`               | Telescope name inserted into the header                                    |
| `-M`, `--headermask` | *bitmask*         | `0xff` (all bits set)       | Mask controlling which information appears in the header. Use `-1` to list bits. |

### Header mask bits (used with `-M`)

| Bit | Description              |
|-----|--------------------------|
| 0   | telescope name           |
| 1   | focuser status           |
| 2   | cooler status            |
| 3   | heater status            |
| 4   | external temperature     |
| 5   | mirror temperature       |
| 6   | measurement time         |

For example, if you are using an external focuser and want to prevent the possibility of its data
being overwritten by the telescope data, use a mask `-M125` (alternatively: `-M0x7D` or `-M0b1111101`).

---

## Socket protocol

The daemon listens for text‑based commands on the configured socket. Each command returns one or
more lines of output, terminated by newline (`\n`).

### Commands

| Command                         | Description                                                                 | Example                           |
|---------------------------------|-----------------------------------------------------------------------------|-----------------------------------|
| `focrel <steps>`                | Move focuser relatively (negative = inward, positive = outward)             | `focrel = -500`                   |
| `focabs <position>`             | Move focuser to absolute position (0 … 65000)                               | `focabs = 32000`                  |
| `focpos`                        | Get current focuser position                                                | `focpos` → `focpos=12345`         |
| `open`                          | Open shutters (only if weather permits and observations not forbidden)      | `open`                            |
| `close`                         | Close shutters                                                              | `close`                           |
| `status`                        | Get shutters state, temperatures, last error, and safety flags              | `status` → (see example below)    |
| `focstop`                       | Immediately stop focuser movement                                           | `focstop`                         |
| `cooler <0/1>`                  | Switch primary mirror cooler off (0) or on (1)                              | `cooler = 1`                      |
| `heater <0/1>`                  | Switch secondary mirror heater off (0) or on (1)                            | `heater = 0`                      |
| `dtime`                         | Return server’s current UNIX time (seconds with milliseconds)               | `dtime` → `UNIXT=1704067200.123`  |
| `help`                          | Show available commands (default handler)                                   | `help`                            |

Each setter returns an `OK` answer in case of success or error code like `BADVAL` for bad value of setter.

### Example `status` response

```
status=closed
measuret=1779086249.480
mirrortemp=6.2
ambienttemp=6.3
```

- `status` can be `opened`, `closed`, `intermediate` or `unknown`.
- `measuret` is the time when the status was last read from the hardware.
- `errored_command` appears if the last attempted command failed.
- `FORBIDDEN` appears when observations have been manually disabled (`SIGUSR1`).
- `BADWEATHER` appears when weather proxy reports unsafe conditions.

---

## Weather integration

The daemon retrieves weather data via the **weather_proxy** shared memory interface (`get_weather_data()`).  
Safety rules:

- If `weather.forceoff`, `weather.rain`, or `weather.weather > WEATHER_TERRIBLE` → shutters are closed and opening is blocked.
- If the weather data is older than **900 seconds** (`WEATHER_LOST`) → treated as lost communication → shutters are closed and blocked.
- When weather becomes good again, the daemon automatically resets the `BADWEATHER` flag and allows normal operation.

---

## Signal handling

| Signal   | Effect (in child process)                           |
|----------|------------------------------------------------------|
| `SIGUSR1`| Forbid observations – close shutters and block open |
| `SIGUSR2`| Allow observations – remove manual block            |
| `SIGTERM`| Stop the daemon and remove PID file                 |
| `SIGINT` | Same as `SIGTERM`                                   |

---

## FITS header file

The daemon writes a text file (default `/tmp/telescope.fits`) with **FITS‑like** keyword/value
pairs. The file is updated every polling cycle (every 2 seconds) and can be used by external
pipelines to record telescope state.

Example content:

```
TELESCOP= 'Astro-M (1)'         / Telescope name
TELSTAT = 'closed'              / Telescope shutters' status
TELCOOLR= 0                     / Primary mirror cooler status: 0/1 (off/on)
TELHEATR= 0                     / Secondary mirror heater status: 0/1 (off/on)
TDOME   = 6.3                   / In-dome temperature, degC
TMIRROR = 6.0                   / Mirror temperature, degC
TTELMEAS= 1779086301.494        / Measurement time: 2026-05-18 09:38:21
```

The file is created atomically using `mkstemp()` + `rename()`.

---

## Building and dependencies

### Dependencies

- [libusefull_macros](https://github.com/eddyem/snippets_library) – common macros and helpers
- [libweather_proxy](https://github.com/eddyem/weather_proxy) – shared memory weather data
- `pthread`, `libc`

### Build

```bash
git clone --depth=1 https://github.com/eddyem/small_tel.git
cd small_tel/Daemons/teldaemon_astrosib
make
```

The resulting binary is `teldaemon_astrosib`. 

### Installation

Copy the binary to a suitable location (e.g. `/usr/local/bin`).  
It is recommended to run the daemon as a system service (using init script, e.g. in `/etc/local.d/`)
so that it starts automatically at boot.

---

## Usage example

Start the daemon with a UNIX socket and weather‑aware header:

```bash
teldaemon_astrosib -u -n "/var/run/teldaemon.sock" \
                   -d /dev/ttyUSB0 -b 115200 \
                   -H /var/state/telescope.fits \
                   -N "Astrosib-500" -M 0x7f
```

Connect to the socket using `socat`, `nc` or my [`tty_term`](https://github.com/eddyem/tty_term):

```bash
socat UNIX-CONNECT:/var/run/teldaemon.sock -
> status
```

Send `SIGUSR1` to forbid observations (e.g. before maintenance):

```bash
kill -USR1 $(cat /tmp/teldaemon.pid)
```

---

## Notes

- The serial protocol is specific to Astrosib hardware (commands like `FOCUSERGO?`,
`SHUTTEROPEN?1,1,1,1,1`, etc. are hard‑coded).
- Focuser absolute position range is **0 … 65000** (values outside this range are ignored).
- The daemon polls the telescope every **2 seconds** (`T_INTERVAL`). During that interval it also
processes client requests and checks weather.
- If the serial device becomes unresponsive, the status will show `unknown` and the header will not
be updated.

---

## License

**GNU General Public License v3.0** or later.  
See the `LICENSE` file in repository's root directory or <http://www.gnu.org/licenses/> for details.
