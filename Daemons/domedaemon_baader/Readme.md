# domedaemon_baader

**domedaemon_baader** is a daemon that controls a [**Baader
Planetarium**](https://www.baader-planetarium.com/) all‑sky dome via a serial line. It provides a
TCP/UNIX socket interface for clients, integrates with a weather proxy to enforce safety rules, and
generates a FITS‑style header file with current dome status and weather sensor data.

---

## Features

- **Dome open/close** – fully open or close the dome  
- **Stop movement** – immediately stop any ongoing dome motion  
- **Weather sensor integration** – reads rain/cloud status from the dome’s internal sensor
- **Error reporting** – reports rain, timeout, and power‑loss error codes  
- **Weather proxy integration** – automatically closes the dome and blocks opening when external
weather is bad or data is stale
- **Manual override** – via `SIGUSR1` (forbid) / `SIGUSR2` (allow) signals  
- **FITS header file** – writes current dome state and weather sensor status into a FITS‑style
header file (e.g., for later use by image processing pipelines)
- **Network interface** – supports both INET (TCP) and UNIX domain sockets  
- **Daemon mode** – forks, writes PID file, restarts child on crash  

---

## Command‑line options

| Option               | Argument          | Default                         | Description                                                                |
|----------------------|-------------------|---------------------------------|----------------------------------------------------------------------------|
| `-h`, `--help`       | –                 | –                               | Show help                                                                  |
| `-d`, `--serialdev`  | *path*            | –                               | Full path to serial device (e.g. `/dev/ttyS1`)                             |
| `-n`, `--node`       | *string*          | –                               | Node specification: IP, `name:IP`, or path for UNIX socket. For anonymous UNIX socket use `\0path` or `@path`. |
| `-l`, `--logfile`    | *path*            | –                               | Full path to log file                                                      |
| `-u`, `--unixsock`   | –                 | 0                               | Use UNIX socket instead of INET                                            |
| `-v`, `--verbose`    | –                 | 0                               | Increase verbosity (each `-v` adds 1 level; 0: only errors, 1: +warnings, 2: +messages, 3: +debug info) |
| `-p`, `--pidfile`    | *path*            | `/tmp/domedaemon.pid`           | PID file path                                                              |
| `-H`, `--headerfile` | *path*            | `/tmp/dome.fits`                | Output file for FITS‑style header                                          |
| `-N`, `--domename`   | *name*            | `Baader`                        | Dome name inserted into the header                                         |
| `-b`, `--baudrate`   | *speed*           | 9600                            | Serial baud rate                                                           |
| `-T`, `--sertmout`   | *microseconds*    | 5000                            | Serial read timeout (µs)                                                   |
| `-m`, `--maxclients` | *number*          | 2                               | Maximum number of simultaneously connected clients                         |

---

## Socket protocol

The daemon listens for text‑based commands on the configured socket. Each command returns one or
more lines of output, terminated by newline (`\n`).

### Commands

| Command                         | Description                                                                 | Example                                      |
|---------------------------------|-----------------------------------------------------------------------------|----------------------------------------------|
| `open`                          | Fully open dome (only if weather permits and observations not forbidden)    | `open`                                       |
| `close`                         | Fully close dome                                                            | `close`                                      |
| `stop`                          | Immediately stop dome movement                                              | `stop`                                       |
| `status`                        | Get dome status and temperatures                                            | `status` → (see example below)               |
| `weather`                       | Get weather sensor status                                                   | `weather` → `weather=good`                   |
| `dtime`                         | Return server’s current UNIX time (seconds with milliseconds)               | `dtime` → `UNIXT=1704067200.123`             |
| `help`                          | Show available commands (default handler)                                   | `help`                                       |

Each setter returns an `OK` answer in case of success or error code like `BADVAL` for bad value of
setter.

### Example `status` response

```
status=opened
measuret=1704067200.123
error=closed@timeout@powerloss
FORBIDDEN
BADWEATHER
errored_command=open
```

- `status` can be `opened`, `closed`, `intermediate` or `unknown`.
- `measuret` is the time when the status was last read from the hardware.
- `error` appears when the dome reports an error. It encodes up to three flags:
  - `closed` – base error (dome closed due to error)
  - `@rain` – rain sensor triggered
  - `@timeout` – watchdog timeout
  - `@powerloss` – power loss detected
- `FORBIDDEN` appears when observations have been manually disabled (`SIGUSR1`).
- `BADWEATHER` appears when weather proxy reports unsafe conditions.
- `errored_command` shows the last command that failed.

---

## Weather integration

The daemon retrieves external weather data via the **weather_proxy** shared memory interface
(`get_weather_data()`).
Safety rules:

- If `weather.forceoff`, `weather.rain`, or `weather.weather > WEATHER_BAD` → dome is closed and opening is blocked.
- If the weather data is older than **300 seconds** (`WEATHER_LOST`) → treated as lost communication → dome is closed and blocked.
- When weather becomes good again, the daemon automatically resets the `BADWEATHER` flag and allows normal operation.

The weather is polled every **5 seconds** (`T_INTERVAL`).

---

## Dome‑internal weather sensor

The daemon also reads the dome’s built‑in weather sensor via the `d#ask_wea` command.  
Possible states:

- `0` → `good` (no rain/clouds)
- `1` → `rain/clouds`
- other → `unknown`

This status is written into the FITS header as `DOMEWEAT`.

---

## Signal handling

| Signal   | Effect (in child process)                           |
|----------|------------------------------------------------------|
| `SIGUSR1`| Forbid observations – close dome and block open     |
| `SIGUSR2`| Allow observations – remove manual block            |
| `SIGTERM`| Stop the daemon and remove PID file                 |
| `SIGINT` | Same as `SIGTERM`                                   |
| `SIGQUIT`| Same as `SIGTERM`                                   |

---

## FITS header file

The daemon writes a text file (default `/tmp/dome.fits`) with **FITS‑like** keyword/value pairs.
The file is updated every polling cycle (every 5 seconds) and can be used by external pipelines to
record dome state.

Example content:

```
OPERATIO= 'FORBIDDEN'           / Observations are forbidden
DOME    = 'Baader'              / Dome manufacturer/name
DOMESTAT= 'closed'              / Dome status
DOMEWEAT= 'good'                / Dome weather sensor status
DOMEECOD= 0                     / Dome error code: Rain|Watchdog|Power
TDOMMEAS= 1779091627.907        / Measurement time: 2026-05-18 11:07:07
```

The `OPERATIO` keyword appears only when observations are manually forbidden via `SIGUSR1`.

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
cd small_tel/Daemons/domedaemon_baader
make
```

The resulting binary is `domedaemon`.

### Installation

Copy the binary to a suitable location (e.g. `/usr/local/bin`).  
It is recommended to run the daemon as a system service (using init script, e.g. in `/etc/local.d/`)
so that it starts automatically at boot.

---

## Usage example

Start the daemon with a TCP local-only socket and weather‑aware header:

```bash
domedaemon -d /dev/ttyS2 -l /var/log/telescope/domedaemon.log -vv \
    -n localhost:55555 -H /tmp/dome.fitsheader
```

Connect to the socket using `socat`, `nc` or my [`tty_term`](https://github.com/eddyem/tty_term):

```bash
nc localhost 55555
> status
```

Send `SIGUSR1` to forbid observations (e.g. before maintenance):

```bash
killall -USR1 domedaemon
```

Don't send signals USR1/USR2 to father process (its PID stored in pidfile): it simply ignores them.
Send them by `killall` to both processes, or by `kill` to daughter process.

---

## Notes

- The serial protocol is specific to Baader Planetarium hardware. Commands such as `d#opendom`, `d#closdom`, `d#stopdom`, `d#get_dom`, `d#ask_wea`, and `d#warning` are hard‑coded in `commands.h`.
- The dome returns numerical values:
  - Position codes: `1111` = fully opened, `2222` = fully closed, anything else = intermediate.
  - Weather codes: `0` = good, `1` = rain/clouds, other = unknown.
  - Error codes are bit‑encoded: `1` = rain, `10` = timeout, `100` = power loss.
- The daemon polls the dome every **5 seconds** (`T_INTERVAL`). During that interval it also processes client requests and checks external weather.
- If the serial device becomes unresponsive, the status will show `unknown` and the header will not be updated.

---

## License

**GNU General Public License v3.0** or later.
See the `LICENSE` file in repository's root directory or <http://www.gnu.org/licenses/> for details.
