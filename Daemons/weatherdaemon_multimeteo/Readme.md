# superweatherdaemon Documentation

## Overview

**superweatherdaemon** is a weather monitoring daemon designed for astronomical observatories. It
collects data from multiple heterogeneous weather stations (meteostations) via a plugin
architecture, computes a unified weather status, and provides control interfaces over TCP and local
UNIX sockets. The daemon can issue warnings, change weather level, and even trigger a forced
shutdown of instruments (e.g., close dome) when conditions become dangerous.

The project is written in C, uses CMake as its build system, and relies on the
[usefull_macros](https://github.com/eddyem/snippets_library) library for utilities and socket
management.

## Dependencies

- **Build tools**: CMake >= 4.0, C compiler with C11 support, `pkg-config`.
- **Library**: `usefull_macros` >= 0.3.5 (`sl_*` functions for logging, sockets, command-line parsing, etc.).
- **Optional**: `net-snmp` (for the SNMP UPS plugin).

On Gentoo/Calculate Linux, install the basic build dependencies:

```bash
emerge dev-build/cmake dev-util/pkgconf
# usefull_macros must be installed from its own source; follow its documentation.
# For SNMP support:
emerge net-analyzer/net-snmp
```

## Building and Installation

1. Clone or download the source tree.
2. Create a build directory and run CMake:

   ```bash
   mkdir build && cd build
   cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
   ```

3. Customise enabled plugins with `-D` options (all are `ON` by default):

   ```bash
   cmake .. -DDUMMY=OFF -DFDEXAMPLE=OFF -DHYDREON=ON -DBTAMETEO=ON ...
   ```

   Available plugin options:
   - `DUMMY`  Dummy weather station for testing.
   - `FDEXAMPLE`  Example file descriptor plugin.
   - `HYDREON`  Hydreon RG-11 rain sensor.
   - `BTAMETEO`  BTA 6-m telescope main meteostation (shared memory).
   - `REINHARDT`  Old Reinhardt meteostation.
   - `WXA100`  Vaisala WXA100 ultrasonic station.
   - `SNMP`  UPS monitoring via SNMP.
   - `LIGHTNING`  AS3935-based lightning sensor.

4. Building:

   ```bash
   make
   su -c "make install"
   ```

   The main executable `superweatherdaemon` is installed into `bin`; the plugin shared libraries
   (`lib*.so`) go to the library directory.

## Configuration

The daemon can be configured entirely via command-line options, a configuration file, or both.
Command-line options take precedence.

### Command-line Options

| Option | Description |
|--------|-------------|
| `-h`, `--help` | Show help message and exit. |
| `-c <file>`, `--conffile <file>` | Use a configuration file. Point non-existant file to get help. |
| `-l <path>`, `--logfile=<path>` | Write logs to a file (default: none). |
| `-P <path>`, `--pidfile=<path>` | PID file (default `/tmp/superweatherdaemon.pid`). |
| `-p <spec>`, `--plugin=<spec>` | Add a weather plugin; can be repeated for different plugins. Format: `path:type:device` (see below). |
| `--port=<node>` | Network port for clients (default `12345`). Use `localhost:port` for local access only. |
| `--sockpath=<path>` | UNIX socket path (start with `@` for an abstract socket). |
| `-T <seconds>`, `--pollt <seconds>` | Max polling interval in seconds (integer). |
| `-v`, `--verb` | Increase verbosity level (each `-v` adds 1). |

### Plugin Specification

A plugin is a shared library (`.so`) that provides a `sensor_init` function. It is loaded with the
`--plugin` option.

Format:

```
--plugin=library:type:parameter
```

- `library`: path to the shared library, e.g. `libwxa100.so`.
- `type`: Connection type  `D` for serial device, `U` for UNIX socket, `N` for INET socket.
- `parameter`: device path and optional speed (`/dev/ttyS0:9600`), UNIX socket name, or `host:port` for INET.

Examples:

```bash
--plugin=libreinhardt.so:D:/dev/ttyS0
--plugin=libwxa100.so:D:/dev/pl2303_0
--plugin=libhydreon.so:D:/dev/ch340_0:1200
--plugin=libbtameteo.so         (no device, uses shared memory)
```

Multiple plugins are listed in order of **importance** (first ones are considered primary for
weather level calculation).

### Configuration File

The configuration file uses a simple `key = value` syntax, with `#` for comments. Example:

```ini
# network port for clients
port = 4444
logfile = /var/log/meteo/superweather.log
verbose = 2
sockpath = "@weather"
pollt = 1
reinit_delay = 10

# Weather thresholds
ahtung_delay = 1800  # in seconds
good_wind = 5.0      # m/s
bad_wind = 10.0
terrible_wind = 15.0
good_humidity = 65.0 # percents
bad_humidity = 87.0
terrible_humidity = 94.0
good_clouds = 2500.0 # for reinhardt sensor in its units
bad_clouds = 2000.0
terrible_clouds = 500.0
clouds_negflag = 1   # 1 means the higher the value the better
good_sky = -40.0     # sky minus ambient temperature, degC
bad_sky = -10.0
terrible_sky = 0.0
# plugins - most important first
plugin = libwxa100.so:D:/dev/pl2303_0
plugin = libhydreon.so:D:/dev/ch340_0:1200
plugin = libbtameteo.so
plugin = libreinhardt.so:D:/dev/ttyS0
```

Run with:

```bash
superweatherdaemon -c /etc/weather.conf
```

To run on system start you can use OpenRó `rc.local` mechanism.

## Plugins

Each weather station is implemented as a shared library exporting a single function:

```c
int sensor_init(sensordata_t *s);
```

The `sensordata_t` structure contains all required callbacks, data pointers, and private fields.
See `weathlib.h` for the full definition.

### Plugin Lifecycle

1. **Loading**: The main daemon calls `s = sensor_new(...)` to create `sensordata_t` structure, 
   opens given library with `dlopen` and calls `sensor_init` on that new `s`.
2. **Initialisation**:
   - Set `s->name`, `s->Nvalues`, `s->values` array.
   - Configure the communication channel (file descriptor) using `getFD(s->path)`.
   - Create a worker thread that periodically reads sensor data and updates `s->values`.
     Don't forget to lock `s->valmutex` on any operation with `s->values`.
3. **Data delivery**: Each time new data is available, call `s->freshdatahandler(s)` 
   (this is set by the daemon to `dumpsensors`). The main daemon then merges the data into the
   global weather evaluation.
4. **Shutdown**: The daemon calls `s->kill(s)`, which must join the thread, close the file
   descriptor, and free resources. Default `common_kill` handles most of this; plugins can override it.
   
If the plugin is disconnected for some reason (for example, the network connection is lost), the
daemon will try to reconnect every `reinit_delay` seconds.

### Available Plugins

| Library | Sensor | Type |
|---------|--------|------|
| `libwsdummy.so` | Dummy station  outputs random walk data around realistic values. | Test / Development |
| `libfdex.so` | Example of a filedescriptor based plugin. Prompts for commaseparated values. | Example |
| `libhydreon.so` | Hydreon RG-11 optical rain sensor. | Serial |
| `libbtameteo.so` | BTA 6-m telescope main meteostation (shared memory). | Shared Memory |
| `libreinhardt.so` | Old Reinhardt meteostation (serial, `?U` command). | Serial |
| `libwxa100.so` | Vaisala WXA100 ultrasonic meteostation (serial, `0R0` command). | Serial |
| `libsnmp.so` | UPS monitor via SNMP (requires net-snmp). | Network |
| `liblightning.so` | AS3935-based lightning sensor (serial). | Serial |

### Writing a New Plugin

Use the existing plugins as templates. A minimal plugin must:

- Include `weathlib.h`.
- Define an array of `val_t` describing each measured quantity.
- Implement `sensor_init`:
  - Allocate `s->values`, copy the template array.
  - Set `s->Nvalues`, `s->name`.
  - Open the device (use `getFD(s->path)` for serial/sockets) and set `s->fdes`.
    If your plugin don't need file descriptor, you must set `s->fdes` to any non-negative value.
  - Create a ring buffer if needed (`sl_RB_new`).
  - Start the worker thread that reads data, updates values inside `pthread_mutex_lock(&s->valmutex)`,
    and calls `s->freshdatahandler(s)` (outside mutex locked).
  - Return `TRUE` on success, `FALSE` on failure (call `s->kill(s)` to clean up).
- The `weathlib.h` provides helper functions: `common_onrefresh`, `common_getval`, `common_kill`.

## Weather Level Calculation

The daemon combines data from all active plugins and continuously evaluates a global **weather
level**:

- `0`  **GOOD**: observations can start safely.
- `1`  **BAD**: risky to start, but can continue.
- `2`  **TERRIBLE**: dome must close, instruments park.
- `3`  **PROHIBITED**: complete shutdown, power off equipment.

### Criteria

Each weather parameter (wind speed, humidity, clouds, sky temperature, lightning distance,
precipitation, etc.) has configurable thresholds:

- `good`  below/above this (depending on sign) the condition is good.
- `bad`  above this the condition is bad.
- `terrible`  above this the condition is terrible.
- `prohibited`  (if defined) above this the condition goes directly to PROHIBITED.
- `negflag`  if `1`, a smaller value is worse (e.g., clouds).
- `shtdnflag`  if `1`, entering the terrible/prohibited range also sets the **FORCE SHUTDOWN** flag.

### Special Flags

- **FORCE SHUTDOWN**: Some parameters (e.g., lightning within <= 5km, UPS on battery) carry the
  `IS_FORCEDSHTDN` meaning and have `shtdnflag = 1`. When their value exceeds the terrible threshold,
  the forced shutdown flag is raised, which immediately sets the weather level to PROHIBITED and is
  typically used to cut power.
- **Manual FORBID**: An operator can send a signal (`SIGUSR1`) or a socket command to forbid
  observations, which also forces PROHIBITED. `SIGUSR2` or a socket command clears forbidden flag.

### Hysteresis

Once a bad/terrible state is reached, the level is not lowered until `ahtung_delay` seconds have
passed since the last serious event. This prevents rapid toggling.

## Server Commands (Socket API)

The daemon listens on two interfaces:

1. **Network socket** (TCP, default port 12345)  read-only (in meaning they cannot change any 
   parameters) access for remote clients.
2. **Local UNIX socket** (abstract or filesystem, default `@weather`)  full control for local applications.

Commands are sent as plain text strings, terminated by a newline.

### Common (read-only) Commands

| Command | Description |
|---------|-------------|
| `get` | Return all collected weather data (all stations). |
| `get=<N>` | Return data from plugin `<N>`. |
| `list` | List all loaded plugins with their names and value counts. |
| `time` | Return server UNIX time (float seconds). |
| `chklevel` | Show the `sense` (importance) level of every collected parameter. |
| `chklevel=<N>` | Same for a specific plugin. |

### Local-only (read-write) Commands

| Command | Description |
|---------|-------------|
| `forbid` | Get current FORBID flag (0/1). |
| `forbid=<0/1>` | Set/clear manual FORBID. |
| `forceoff` | Get FORCE SHUTDOWN flag. |
| `forceoff=<0/1>` | Set/clear it manually. |
| `weathlevel` | Get current weather level (0-3 for GOOD-PROHIBITED). |
| `weathlevel=<0..3>` | Force weather level (use with caution). |
| `setlevel=<plugin>:<param>=<sense>,...` | Change the `sense` field of one or more sensor parameters. Example: `setlevel=1:WIND=3,HUMIDITY=3` disables wind and humidity from station 1. |
| `mute=<N>` | Stop refreshing data from plugin `<N>` (mute). |
| `unmute=<N>` | Resume refreshing. |
| `ismuted=<N>` | Return 1 if muted, 0 otherwise. |

Reply format: each line is a FITS-like `KEY = value / comment` string; commands that set something
usually echo back the variable and its new value. For `get=<N>` each `KEY` have a suffix in square
brackets  number of plugin, e.g. `WIND[1]= 10.1 / Wind speed, m/s`.

## Signals

| Signal | Effect |
|--------|--------|
| `SIGTERM`, `SIGINT`, `SIGQUIT` | Clean shutdown (removes PID file, kills plugins, destroys sockets). |
| `SIGHUP` | Ignored. |
| `SIGUSR1` | Set manual FORBID (weather level == PROHIBITED). |
| `SIGUSR2` | Clear manual FORBID. |
| `SIGPIPE` | Logged, used to detect network plugins disconnections. |

## Files

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Top-level build definition. |
| `cmdlnopts.c/.h` | Command-line and configuration file parsing. |
| `main.c` | Daemon entry point, signal handlers, forking. |
| `mainweather.c/.h` | Global weather evaluation, data collection, forced shutdown. |
| `sensors.c/.h` | Plugin management (load, unload, getters). |
| `server.c/.h` | TCP and UNIX socket servers. |
| `weathlib.c/.h` | Common plugin API, value definitions, helper functions. |
| `fd.c` | Function `getFD()` to open serial devices or sockets for plugins. |
| `example.config` | Sample configuration file. |
| `plugins/CMakeLists.txt` | Build file for all plugins. |
| `plugins/*.c` | Individual plugin source files. |

## License

The project is released under the **GNU General Public License v3.0** or later. See the headers in
the source files for the full legal text.

---

*For further assistance or to report issues, please contact the maintainer: Edward V. Emelianov
<edward.emelianoff@gmail.com>.*
