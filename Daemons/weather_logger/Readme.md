# meteologger

**meteologger** is a client that collects meteorological data from the
[`weatherdaemon_multimeteo`](https://github.com/eddyem/small_tel/tree/master/Daemons/weatherdaemon_multimeteo)
daemon. It connects to the daemon over a TCP or UNIX socket, periodically requests data from all
available weather stations, and saves the received values into separate files (one per station).

The project is written in C and uses the
[`usefull_macros`](https://github.com/eddyem/snippets_library) library for socket handling,
logging, argument parsing, and helper macros.

## Repository

Source code is located in the [small_tel](https://github.com/eddyem/small_tel) repository under
`Daemons/weather_logger`.

### Dependencies

- Linux (uses Linux‑specific calls: `prctl`, `fork`, `waitpid`, signals)
- CMake ≥ 4.0
- C compiler (GCC, Clang)
- `usefull_macros` library ≥ 0.3.5 (must be installed and discoverable via `pkg-config`)

### Building

```bash
git clone --depth=1 https://github.com/eddyem/small_tel.git
cd small_tel/Daemons/weather_logger
mkdir build && cd build
cmake ..    # or -DDEBUG=on for debug build
make
```

The executable `meteologger` will be placed in `build/` (or `bin/` after installation).

### Installation

```bash
su -c "make install"
```

By default the program is installed into `bin` (usually `/usr/local/bin`).

## Usage

```
meteologger -n <node> -o <directory> [options]
```

### Required arguments

| Option | Long form      | Description |
|--------|----------------|-------------|
| `-n`   | `--node`       | Node to connect to: `<host>:<port>` (e.g., `127.0.0.1:5555`) or a UNIX socket path when `-u` is given. |
| `-o`   | `--output`     | Directory where database files will be stored (must exist and be writable). |

### Optional arguments

| Option | Long form      | Default                     | Description |
|--------|----------------|-----------------------------|-------------|
| `-h`   | `--help`       | -                           | Show help and exit. |
| `-u`   | `--isunix`     | -                           | Use a UNIX socket instead of TCP. |
| `-l`   | `--logfile`    | (none)                      | File to write logs |
| `-p`   | `--pidfile`    | `/tmp/meteologger.pid`      | File where the process PID is written. |
| `-v`   | `--verbose`    | 0                           | Increase logging verbosity (each `-v` raises the level). |
| `-i`   | `--interval`   | `0.5`                       | Request interval in seconds. Allowed range: `[0.2, 900]`. |
| `-t`   | `--timeout`    | `1.0`                       | Network timeout for server responses (seconds). Allowed range: `[0.1, 30]`. |

### Examples

1. Connect to a local daemon on port 5555 and store data in `/var/log/weather`:
    ```bash
    meteologger -n 127.0.0.1:5555 -o /var/log/weather
    ```
   
2. Use a UNIX socket `/tmp/weather.sock`, write PID to `/run/meteologger.pid`:
    ```bash
    meteologger -n /tmp/weather.sock -o /data/weather -u -p /run/meteologger.pid
    ```
    
3. Enable verbose logging to a file:
    ```bash
    meteologger -n localhost:5555 -o /srv/weather -vv -l /var/log/meteologger.log
    ```

## Output file format

Files are stored in the directory given by `-o` and are named `weatherXX.log`, where `XX` is a
two‑digit station number (from `00` to `99`). If a file already exists for a station (matching the
station name in the header comment), new data is appended.

File structure:

```
# <station_name>
# Station #<number>, format: KEYWORD[level],...
# TIMESTAMP, <key1>[<level1>], <key2>[<level2>], ...
<timestamp>, <value1>, <value2>, ...
```

Example:

```
# WXA100-06 ultrasonic meteostation @ D:/dev/pl2303_0
# Station #0, format: KEYWORD[level],...
# TIMESTAMP, WIND[1], WINDDIR[2], HUMIDITY[1], EXTTEMP[1], PRESSURE[1], PRECIP[3], PRECIPLV[3], PRECRATE[3]
1779369905, 0.50, 120.70, 76.90, 8.40, 590.00, 1, 374.70, 0.00
1779369907, 0.60, 128.20, 76.90, 8.40, 590.00, 1, 374.70, 0.00
1779369909, 0.70, 130.30, 76.90, 8.40, 590.00, 1, 374.70, 0.00
...
```

- First line: comment with station name.
- Second line: station number and format description.
- Third line: column headers: `TIMESTAMP` followed by  keys with their levels.
- Following lines: data – Unix timestamp (integer) and values (floating‑point, integer or string).

-----

## License

Copyright © 2026 Edward V. Emelianov.
**GNU General Public License v3.0** or later.

See the `LICENSE` file in repository's root directory or [http://www.gnu.org/licenses/](http://www.gnu.org/licenses/)
for details.

