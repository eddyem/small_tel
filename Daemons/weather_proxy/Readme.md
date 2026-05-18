The `weather_proxy` package offers a lightweight daemon (`weather_proxy`) that receives data from
[`supermeteodaemon`](https://github.com/eddyem/small_tel/tree/master/Daemons/weatherdaemon_multimeteo)
via a TCP or UNIX socket, parses it, and makes it available locally through a
shared memory segment. A client library (`libweather.so`) and a simple command-line utility
(`chkweather`) are also provided to easily retrieve and check weather conditions.

---

## Overview

The main daemon (`weather_proxy`) connects to a running `supermeteodaemon` process, parses the
incoming FITS-header-style data stream, and regularly updates a POSIX shared memory segment
(`/weather_shm`). A named semaphore (`/weather_sem`) protects the shared data. The client library
offers the `get_weather_data()` function to read this shared memory and return the data in a
`weather_data_t` structure. The `chkweather` utility uses this function to quickly check the
current weather status and produce an exit code suitable for scripting.

## Components

The project consists of the following source files:

| File | Description |
|------|-------------|
| `weather_daemon.c` | The main proxy daemon. |
| `weather_client.c` | The client library (`libweather.so`) with `get_weather_data()`. |
| `weather_data.h` | Common data structure and definitions. |
| `chkweather.c` | A simple command‑line tool to check weather status. |
| `weather_clt_example.c` | Example usage of the client library. |
| `Makefile` | Builds the daemon, client library, and utilities. |

## Building

The package requires a C compiler (e.g., `gcc`) and the [`usefull_macros` library](https://github.com/eddyem/snippets_library). 
To install the latter clone its code and use `cmake`/`make`:

```bash
git clone --depth=1 https://github.com/eddyem/snippets_library
cd snippets_library
mkdir build
cd build
cmake ..
make
su -c "make install"
# or "sudo make install" if you have "sudo" configured
```

Then build the project:

```bash
make
```

This produces:
- `weather_proxy` – the main daemon
- `libweather.so` – the shared client library
- `chkweather` – the weather check utility
- `weather_clt_example` – an example client

To install:

```bash
su -c "make install"
```

This copies `libweather.so` to `/usr/local/lib`, installs `weather_data.h` in `/usr/local/include`,
and places the executables in `/usr/local/bin`.

## Usage

### Running the proxy daemon (`weather_proxy`)

```bash
weather_proxy -n <node> [-u] [-l <logfile>] [-p <pidfile>] [-f <fitsheader>] [-v]
```

#### Options

| Option | Description |
|--------|-------------|
| `-n <node>` | Connection endpoint. For TCP: `host:port`. For UNIX sockets: path to socket. Required. |
| `-u` | Use a UNIX socket instead of TCP. |
| `-l <file>` | Write log messages to the specified file. |
| `-p <file>` | PID file (default `/tmp/weather_proxy.pid`). |
| `-f <file>` | Save the latest weather data as a FITS‑header file. |
| `-v` | Increase verbosity (can be used multiple times). |

#### Example

Connect to `supermeteodaemon` on TCP port 12345 at `localhost`:

```bash
weather_proxy -n localhost:12345 -v -l /var/log/weather_proxy.log
```

Use a UNIX socket and store the FITS header:

```bash
weather_proxy -n /tmp/superweather.sock -u -f /tmp/latest_weather.header
```

### Checking weather (`chkweather`)

`chkweather` reads the shared memory and prints the current weather data. It returns an exit code
according to the weather condition:

| Exit Code | Meaning |
|-----------|---------|
| `0` | Weather is good – observations may start. |
| `1` | Weather is bad – cannot start, but ongoing observations may continue. |
| `2` | Weather is terrible – close the dome, park. |
| `3` | Weather is prohibited – power off equipment, prepare to shut down the computer (or data is too old). |

Example:

```bash
if ./chkweather; then
    echo "Conditions are good - starting observations"
else
    echo "Bad weather - stopping"
fi
```

### Using the client library in your own code

The library provides a single function:

```c
#include "weather_data.h"

int get_weather_data(weather_data_t *data);
```

It fills the `weather_data_t` structure with the latest weather information. Returns `0` on
success, `-1` on failure.

#### `weather_data_t` structure

```c
typedef struct {
    weather_condition_t weather;    // condition (0..3)
    float windmax;                  // max wind in last hour (m/s)
    float wind;                     // current wind speed (m/s)
    float clouds;                   // sky "quality" (>2500 = good)
    float exttemp;                  // external temperature (°C)
    float pressure;                 // atmospheric pressure (mmHg)
    float humidity;                 // relative humidity (%)
    int rain;                       // 1 if rain is detected
    int forceoff;                   // forced power‑off flag
    time_t last_update;             // timestamp of the data
} weather_data_t;
```

The `weather_condition_t` enum:

| Value | Meaning |
|-------|---------|
| `WEATHER_GOOD` (0) | Clear for observations |
| `WEATHER_BAD` (1) | Unfavourable but not critical |
| `WEATHER_TERRIBLE` (2) | Dangerous conditions – close and park |
| `WEATHER_PROHIBITED` (3) | Emergency – close, park and prepare for powering off |

Example client:

```c
#include "weather_data.h"
#include <stdio.h>

int main() {
    weather_data_t wd;
    if (get_weather_data(&wd) == 0) {
        printf("Weather: %d, Temp: %.1f C\n", wd.weather, wd.exttemp);
    }
    return 0;
}
```

Compile with:

```bash
gcc -o myclient myclient.c -lweather
```

## Data Flow

1. **supermeteodaemon** collects data from various sensors and outputs it as lines of `KEY=VALUE`
pairs (FITS-header style).
2. **weather_proxy** connects to the socket, sends a `"get\n"` command, and parses the incoming
lines.
3. It extracts the fields: `WEATHER`, `WINDMAX1`, `PRECIP`, `CLOUDS`, `WIND`, `EXTTEMP`,
`PRESSURE`, `HUMIDITY`, `FORCEOFF`, and `TMEAS` (the timestamp).
4. The data is stored in a POSIX shared memory segment (`/weather_shm`) and is protected by a named
semaphore (`/weather_sem`).
5. Client applications (like `chkweather` or your own code) use `get_weather_data()` to read the
shared memory safely.

## Signals

The daemon responds to:

- **SIGUSR1** – Forbid observations (sets an internal `forbidden` flag which overrides the current
weather to `WEATHER_PROHIBITED`).
- **SIGUSR2** – Permit observations (clears the `forbidden` flag).

These signals are only handled by the worker child process; the parent monitor process ignores
them. So you can send them simply by `killall -sUSR1 weather_proxy`.

## FITS‑Header Output

If the `-f <file>` option is given, the daemon writes each received `KEY=VALUE` line to a temporary
file and, when the data set is complete (i.e., the `TWEATH` line is received), it atomically
replaces the target file. This produces a plain‑text file that can be used as a FITS header or for
other applications.

Example generated file:

```
WIND    =                 4.30 / Wind, m/s
WINDMAX =                 5.90 / Maximal wind speed for last 24 hours
WINDMAX1=                 5.90 / Maximal wind speed for last hour
WINDDIR =                32.75 / Wind direction, degr (CW from north to FROM)
WINDDIR1=                25.97 / Mean wind speed direction for last hour
WINDDIR2=                35.37 / Mean wind speed^2 direction for last hour
HUMIDITY=                57.50 / Humidity, percent
EXTTEMP =                 9.00 / Ambient temperature, degC
PRESSURE=               585.72 / Atmospheric pressure, mmHg
PRECIP  =                    0 / Precipitation (1 - yes, 0 - no)
PRECIPLV=                 0.00 / Cumulative precipitation level (mm)
CLOUDS  =              2399.50 / Integral sky quality value (bigger - better)
WEATHER =                    0 / Weather (0..3: good/bad/terrible/prohibited)
TSINCERN=                   20 / Minutes since rain (20 means a lot of)
RAINPOW =                    0 / Rain strength, 0..255
RAINAVG =                    0 / Average rain strength, 0..255
RSAMBL  =                   48 / Ambient light by rain sensor, 0..255
RSFREEZ =                    0 / Rain sensor is freezed
TWEATH  =           1779085104 / Last weather time: 2026-05-18 09:18:24
```

----

## License

Copyright © 2026 Edward V. Emelianov.
**GNU General Public License v3.0** or later.
See the `LICENSE` file in repository's root directory or [http://www.gnu.org/licenses/](http://www.gnu.org/licenses/)
for details.
