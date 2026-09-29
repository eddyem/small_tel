# mountdaemon_10micron

A daemon for controlling 10Micron equatorial mounts (e.g. GM4000HPS) over a serial port. It exposes
a text command interface for local clients, a binary Stellarium-protocol interface for planetarium
software, and periodically publishes the current telescope state into a FITS-header file for
downstream acquisition software.

The daemon is written in C (C23), uses [ERFA](https://github.com/liberfa/erfa) for all astronomical
computations, [usefull_macros](https://github.com/eddyem/snippets_library) for utility routines,
and optionally [weather
proxy](https://github.com/eddyem/small_tel/tree/master/Daemons/weather_proxy) for getting meteo data.

---

## Table of contents

1. [Overview](#overview)
2. [Building and installation](#building-and-installation)
3. [Running the daemon](#running-the-daemon)
4. [Command-line options and configuration file](#command-line-options-and-configuration-file)
5. [Astronomical parameters](#astronomical-parameters)
6. [Coordinate systems](#coordinate-systems)
7. [Network interfaces](#network-interfaces)
   - [Command socket](#command-socket)
   - [Stellarium socket](#stellarium-socket)
8. [Command reference](#command-reference)
9. [FITS-header file](#fits-header-file)
10. [Emulation mode](#emulation-mode)
11. [Signals and process management](#signals-and-process-management)
12. [Architecture](#architecture)
13. [Known limitations](#known-limitations)

---

## Overview

`mountdaemon_10micron` connects to a 10Micron mount over a serial line (default `/dev/ttyUSB0` at
115200 baud) and performs three independent jobs:

1. **Serves a local text command interface** — clients send newline-terminated commands (`tagra`,
`gotord`, `park`, …) over a TCP or UNIX socket and receive plain `key=value` replies. This is the
primary interface for an observatory control system.
2. **Serves a Stellarium-compatible binary protocol** — a planetarium program (Stellarium, Cartes
du Ciel, etc.) can connect to a second TCP socket, read the current telescope position, and issue
goto commands. This allows visual pointing with the planetarium.
3. **Writes a FITS-header file** — at least every `MOUNT_CHECK_T` seconds the daemon writes a fresh
FITS-header block with the current telescope position, sidereal time, MJD and place data.
The file is atomically replaced so a downstream acquisition program can read it at any moment.

Additionally, the daemon:
- corrects the mount's internal clock and refraction model once per hour from local weather data;
- parks the mount automatically if the weather feed is lost while tracking;
- reconnects to the mount if the serial link drops;
- supports an **emulation mode**, in which the mount is simulated locally — useful for testing
clients without hardware.

The daemon is built around the ERFA library, so coordinates are rigorously converted between ICRS
(J2000), CIRS, observed, and horizontal systems, and refraction is accounted for using the current
pressure and temperature.

---

## Building and installation

### Dependencies

- a C23-capable compiler (GCC 13+ or Clang 16+);
- CMake ≥ 4.0;
- **pkg-config**;
- **liberfa** ≥ 2.0.0;
- **usefull_macros** ≥ 0.3.5;
- **libweather** (linked with `-lweather`);
- `libm`.

### Build

```sh
git clone --depth=1 https://github.com/eddyem/small_tel
cd Daemons/10micron_stellarium
mkdir build && cd build
cmake ..
make
```

Options recognised by CMake:

| Option   | Default | Meaning                                           |
|----------|---------|---------------------------------------------------|
| `DEBUG`  | `OFF`   | Build in debug mode (`-Og -g3 -Werror`, no fork). |

### Install

```sh
su -c "make install"
```

The binary is installed to `<prefix>/bin/mountdaemon_10micron`. The default prefix is `/usr/local`.

---

## Running the daemon

```sh
mountdaemon_10micron -d /dev/ttyS0 -S 9600 \
                     --latitude 43.65:39:00 --longitude 41.44:06:00 --altitude 2070 \
                     --dut1 0.1 -p :10000 -P localhost:10001 -v
```

The daemon forks a supervisor process that re-spawns the working process if it crashes. It writes a
PID-file (`/tmp/mountdaemon_10micron.pid` by default), which is removed on clean exit.

Stopping the daemon: send `SIGTERM` (or `SIGINT`, `SIGQUIT`) to the supervisor PID from
the PID-file. The supervisor kills its child and exits.

---

## Command-line options and configuration file

All options may be given either on the command line or in a configuration file passed with `-c
<file>`. Command-line values always take precedence over the configuration file. The config file
uses `key = value` lines — key names are the long option names below without the leading `--`.

| Long option        | Short | Argument | Default                          | Description                                                            |
|--------------------|-------|----------|----------------------------------|------------------------------------------------------------------------|
| `--device`         | `-d`  | string   | `/dev/ttyUSB0`                   | Serial device connected to the mount.                                  |
| `--emulation`      | `-e`  | —        | `0`                              | Run in emulation mode (no hardware needed).                            |
| `--logfile`        | `-l`  | string   | no                               | Redirect log output to this file.                                      |
| `--hdrfile`        | `-o`  | string   | `/tmp/10micron.fitsheader`       | File to write the FITS-header into.                                    |
| `--pidfile`        |       | string   | `/tmp/mountdaemon_10micron.pid`  | PID-file path.                                                         |
| `--port`           | `-p`  | string   | `:10000`                         | Port (or `host:port`) for the Stellarium server.                       |
| `--cmdport`        | `-P`  | string   | `localhost:10001`                | Port (or UNIX-socket path) for the command console.                    |
| `--sleept`         | `-t`  | int      | `100`                            | Main-loop sleep, µs.                                                   |
| `--isunix`         | `-U`  | —        | `0`                              | Use a UNIX-domain socket for `cmdport`.                                |
| `--sertmout`       | `-T`  | double   | `1.0`                            | Serial answer timeout, seconds.                                        |
| `--serspeed`       | `-S`  | int      | `115200`                         | Serial speed.                                                          |
| `--maxclients`     |       | int      | `5`                              | Max clients per socket.                                                |
| `--mountname`      |       | string   | `"10Micron GM4000HPS"`           | Mount name for the FITS header.                                        |
| `--verbose`        | `-v`  | —        | `0`                              | Increase verbosity (each `-v` adds a level).                           |
| `--parka`          | `-A`  | string   | built-in                         | Parking azimuth, degrees or `DD:MM:SS`.                                |
| `--parkz`          | `-Z`  | string   | built-in                         | Parking zenith distance.                                               |
| `--dut1`           |       | double   | `0`                              | UT1 − UTC, seconds.                                                    |
| `--polarx`         |       | double   | `0`                              | IERS polar-motion X, arcsec.                                           |
| `--polary`         |       | double   | `0`                              | IERS polar-motion Y, arcsec.                                           |
| `--latitude`       |       | string   | built-in                         | Site latitude (degrees, `DD:MM:SS`, or decimal).                       |
| `--longitude`      |       | string   | built-in                         | Site longitude (degrees, positive east).                               |
| `--altitude`       |       | string   | built-in                         | Site altitude, metres.                                                 |
| `--help`           | `-h`  | —        | —                                | Show help.                                                             |
| `--config`         | `-c`  | string   | —                                | Read options from this file.                                           |

### Example configuration file

```ini
device     = /dev/serial/by-id/usb-FTDI_USB-RS232-if00-port0
serspeed   = 115200
sertmout   = 1.5
port       = :10000
cmdport    = /tmp/mountdaemon.sock
isunix     = 1
sleept     = 200
maxclients = 10
latitude   = 43.65:39:00
longitude  = 41.44:06:00
altitude   = 2070
dut1       = 0.1
polarx     = 0.05
polary     = 0.12
mountname  = "10Micron GM4000HPS"
logfile    = /var/log/mountdaemon.log
```

---

## Astronomical parameters

The following parameters influence pointing accuracy and FITS-header content. They may be set at
start-up via CLI/config, and (except place data) also at runtime via network commands.

| Parameter       | Runtime command | Units    | Typical range      | Notes                                                     |
|-----------------|-----------------|----------|--------------------|-----------------------------------------------------------|
| Site latitude   | —               | degrees  | −90 … +90          | Set at start-up only.                                     |
| Site longitude  | —               | degrees  | −180 … +180 (E+)   | Set at start-up only.                                     |
| Site altitude   | —               | metres   | −500 … +9000       | Set at start-up only.                                     |
| DUT1            | `dut1`          | seconds  | −1 … +1            | UT1 − UTC. Affects LST by up to ±15″, affecting pointing. |
| Polar motion X  | `polarx`        | arcsec   | ±1000              | IERS X coordinate of celestial pole.                      |
| Polar motion Y  | `polary`        | arcsec   | ±1000              | IERS Y coordinate of celestial pole.                      |

If not set, the daemon uses internal defaults (site: SAO RAS, altitude 2070 m). DUT1 and polar
motion default to 0.

---

## Coordinate systems

The daemon distinguishes the following frames:

| Frame                       | Notation   | Units    | Notes                                                    |
|-----------------------------|------------|----------|----------------------------------------------------------|
| Catalog (ICRS / J2000)      | `J2000`    | RA: hours, Dec: degrees | Coordinates entered by the user via `tagra/tagdec`.       |
| Epoch of date (mean equinox)| `Jnow`     | RA: hours, Dec: degrees | Used internally for pointing.                             |
| CIRS (geocentric apparent)  | —          | radians  | Result of `eraAtci13`.                                    |
| Observed (with refraction)  | —          | radians  | Result of `eraAtco13`.                                    |
| Horizontal                  | `Az/Zd`    | degrees  | Azimuth, clockwise from North. ZD = 90° − altitude.       |

- **Target epoch** is stored as an MJD. By default it is J2000 (`ERFA_DJM00` = 51544.5). It can be
set to any epoch via the `tagmjd` command.
- The command `gotord` converts the input coordinates from the target epoch to *Jnow* using
`JXtoJnow()` before sending to the mount.
- The command `gotorh` interprets the input as an **hour angle** and converts to RA using the
current LST.
- The current telescope position returned by the mount is in *Jnow*, and the FITS-header records it
under `RA`, `HA`, `DEC`, `AZ`, `ZD`.

---

## Network interfaces

### Command socket

Default address: `localhost:10001` (TCP) or a UNIX socket if `--isunix` is set.

Protocol: **line-oriented text**. A client sends one command per line, optionally with a value:

```
tagra = 12.345
tagdec = +45:30:00
gotord
```

The daemon replies with `key=value` (newline-terminated) for getters, or nothing (only implicit
success/failure) for setters/actions. Commands may be issued one at a time or in a stream. All
sockets are multiplexed by a thread-per-client model inside `usefull_macros`'s `sl_sock`
infrastructure.

Up to `--maxclients` simultaneous clients are accepted. If more try to connect, they receive `Try
later: too much clients connected` and the connection is closed.

Values may be given either as a decimal number (`12.345`, `-26.5`) or as `DD:MM:SS` / `HH:MM:SS`
(sexagesimal). Angles are interpreted as degrees unless the field name (RA, HA, LST) explicitly
implies hours.

### Stellarium socket

Default address: `:10000` (all interfaces, port 10000). Protocol: **binary little-endian**,
according to Stellarium's "Telescope Control" plugin specification.

Incoming (client → server) message, 20 bytes:

| Field  | Type      | Meaning                                                |
|--------|-----------|--------------------------------------------------------|
| `len`  | `uint16`  | Total message length (`20`).                            |
| `type` | `uint16`  | `0`.                                                    |
| `time` | `uint64`  | µs since epoch (unused).                                |
| `RA`   | `uint32`  | Target RA. `0` = 0h, `0x80000000` = 12h, `0x100000000` = 24h. |
| `DEC`  | `int32`   | Target Dec. `-0x40000000` = −90°, `0x40000000` = +90°. |

Outgoing (server → client) message, 24 bytes: same fields plus a trailing `int32 status` (0 = ok,
other = error).

The daemon treats incoming messages as `tagra`/`tagdec` in the current epoch (which for Stellarium
is normally J2000). It does **not** automatically start a slew — the client must send `gotord` on
the command socket.

---

## Command reference

All commands are case-sensitive. Getter command names are typically used without an argument;
setter commands require an argument of the indicated type.

### Target coordinates (input)

| Command   | Type   | Units              | Description                                                      |
|-----------|--------|--------------------|------------------------------------------------------------------|
| `tagra`   | get/set| hours (0…24)       | Target right ascension.                                          |
| `tagdec`  | get/set| degrees (−90…+90)  | Target declination.                                              |
| `tagha`   | get/set| hours              | Target hour angle (alternative to RA).                           |
| `tagaz`   | get/set| degrees (0…360)    | Target azimuth (clock from North).                               |
| `tagzd`   | get/set| degrees (0…90)     | Target zenith distance.                                          |
| `tagmjd`  | get/set| MJD or `J<year>`   | Epoch of the input coordinates. `J2050` = 2050.0, `51544.5` = J2000. |

### Pointing

| Command   | Description                                                                                 |
|-----------|---------------------------------------------------------------------------------------------|
| `gotord`  | Slew to the input RA/Dec (converted from target epoch to Jnow) and start tracking.          |
| `gotorh`  | Slew using the stored hour angle: RA = LST − HA, then track.                                |
| `gotoaz`  | Slew to the input Az/ZD and stop.                                                           |
| `stop`    | Stop any motion (emergency stop).                                                           |
| `stoptrk` | Stop tracking but leave the mount in place.                                                 |
| `track`   | Start tracking from the current position.                                                   |
| `park`    | Slew to the parking position (see `parkaz`/`parkzd`).                                       |
| `shutdown`| Power off the mount. Requires a numeric key to confirm (see below).                         |

### State queries

| Command   | Units    | Description                                                     |
|-----------|----------|-----------------------------------------------------------------|
| `status`  | —        | Human-readable mount status.                                    |
| `telra`   | hours    | Current telescope RA (Jnow).                                    |
| `telha`   | hours    | Current telescope hour angle.                                   |
| `teldec`  | degrees  | Current telescope declination (Jnow).                           |
| `telaz`   | degrees  | Current telescope azimuth.                                      |
| `telzd`   | degrees  | Current telescope zenith distance.                              |
| `lst`     | hours    | Local sidereal time.                                            |
| `unixt`   | seconds  | Server UNIX time.                                               |

`tel*` commands return `RESULT_FAIL` if the weather feed is lost or the mount is in `Error` state —
this prevents a client from acting on stale coordinates.

### Place and almanac data

| Command   | Type   | Description                                                     |
|-----------|--------|-----------------------------------------------------------------|
| `place`   | get    | Site latitude, longitude, altitude.                             |
| `dut1`    | get/set| UT1 − UTC, seconds.                                             |
| `polarx`  | get/set| IERS X pole coordinate, arcsec.                                 |
| `polary`  | get/set| IERS Y pole coordinate, arcsec.                                 |

Place data can only be set at start-up (via CLI/config).

### Parking

| Command   | Type   | Description                          |
|-----------|--------|--------------------------------------|
| `parkaz`  | get/set| Parking azimuth, degrees.            |
| `parkzd`  | get/set| Parking zenith distance, degrees.    |

### Sending custom command

The `raw` command allows to send any unsupported command string directly to mount.
E.g. to set lunar tracking rate send `raw = :TL#`. If mount gives no answer for command, you will
get message "No answer", otherwise you'll get this answer.

### Shutdown

The `shutdown` command implements a simple confirmation handshake:

1. A client sends `shutdown` **without** an argument. The daemon generates a random key, stores it
with a timestamp, and returns `shutdown=<key>`.
2. Within 5 minutes, a client must send `shutdown = <key>`. If the key matches, the mount is powered
off.

This protects against accidental shutdown commands.

---

## FITS-header file

Every `MOUNT_CHECK_T` (0.5 s) the daemon collects the current state and writes a complete
FITS-header block into the file given by `--hdrfile`. The write is atomic: a temporary file is
created with `mkstemp()` and `rename()`d over the destination, so a reader never sees a partial
block.

The following keywords are written (only if meaningful):

| Keyword          | Comment                                                              |
|------------------|----------------------------------------------------------------------|
| `TIMESYS`        | `'UTC'`.                                                             |
| `ORIGIN`         | `'SAO RAS'`.                                                         |
| `MOUNTNAM`       | Mount name (from `--mountname`).                                     |
| `POLARX`         | X pole coordinate, arcsec (if non-zero).                             |
| `POLARY`         | Y pole coordinate, arcsec (if non-zero).                             |
| `DUT1`           | UT1 − UTC, seconds (if non-zero).                                    |
| `INPRA`,`INPDEC` | Input target RA/Dec, if the last input was celestial.                |
| `INPAZ`,`INPZD`  | Input target Az/ZD, if the last input was horizontal.                |
| `TAGRA`,`TAGDEC` | Last **slewed-to** target, always celestial.                         |
| `RA`,`HA`,`DEC`  | Current telescope position in *Jnow*.                                |
| `AZ`,`ZD`        | Current telescope position in the horizontal frame.                  |
| `TELSTAT`        | Human-readable mount status.                                         |
| `INPEQUIN`       | Epoch (year) of the input coordinates.                               |
| `EQUINOX`        | Epoch (year) of the current telescope coordinates.                   |
| `MJD`            | MJD of the header.                                                   |
| `PIERSIDE`       | Pier side of the mount (`'E'`/`'W'`).                                |
| `ELEVAT`         | Site altitude, m.                                                    |
| `LONGITUD`       | Site longitude, degrees east.                                        |
| `LATITUDE`       | Site latitude, degrees north.                                        |
| `LSTEND`         | Local sidereal time, hours.                                          |

Weather keywords (`HUMIDITY`, `PRESSURE`, `EXTTEMP`, `RAIN`, `SKYQUAL`, `WINDSPD`, `WINDMAX`,
`WEATTIME`) are intentionally **not** written by this daemon — the weather data is expected to be
merged by the weather daemon's own header writer.

---

## Emulation mode

Started with `-e`/`--emulation`, the daemon does not open the serial port and instead simulates a
mount with the following behaviour:

- Start-up position: Az = 180°, ZD = 80°.
- Slew rates: 5°/s in RA, 8°/s in Dec.
- Slew completes when the angular distance is below 1″ or when the modelled time has elapsed.
- `ZD_LIMIT = 80°` — slews beyond this limit are refused or stopped.
- **Meridian flip** is simulated: if reaching the target from the "flipped" side is faster, the
mount will go through a flipped state (Dec > 90°, RA += 12h).
- Statuses `Stopped`, `Slewing`, `Tracking` are fully modelled; the others are not.

This is useful for developing clients (control-system, planetarium) without access to real
hardware. All command-socket and Stellarium-socket behaviour is identical to the real-mount mode.

---

## Signals and process management

The daemon runs as a supervisor + worker pair:

- The **supervisor** (`main()` before `fork()`) watches the child, re-spawns it after a crash, and
holds the PID-file.
- The **worker** is the actual server: it opens the serial port, listens on sockets, and processes
commands.

Signals handled:

| Signal      | Action                                                                |
|-------------|-----------------------------------------------------------------------|
| `SIGTERM`   | Remove PID-file, kill worker, exit.                                   |
| `SIGINT`    | Same as `SIGTERM`.                                                    |
| `SIGQUIT`   | Same as `SIGTERM`.                                                    |
| `SIGHUP`    | Ignored (may be used later for config-file re-reading)                |
| `SIGTSTP`   | Ignored (so the daemon can survive `Ctrl-Z` from a shell session).    |

If the worker dies with a non-zero exit status, the supervisor logs the event. If the worker dies
within 10 minutes of the previous restart, this is treated as a crash-loop and logged with a
warning.

---

## Architecture

```
                  ┌─────────────────────────┐
                  │     supervisor (fork)   │
                  └────────────┬────────────┘
                               │ fork + waitpid
                  ┌────────────▼────────────┐
   cmd clients ──►│       worker process    │◄── Stellarium clients
                  │                         │
                  │  ┌───────────────────┐  │
                  │  │  cmd_socket (TCP  │  │
                  │  │  or UNIX, thread  │  │
                  │  │  per client)      │  │
                  │  └───────────────────┘  │
                  │  ┌───────────────────┐  │
                  │  │  stellarium_sock  │  │
                  │  │  (TCP, thread     │  │
                  │  │  per client)      │  │
                  │  └───────────────────┘  │
                  │  ┌───────────────────┐  │
                  │  │  main loop        │  │
                  │  │  (0.5 s tick)     │  │
                  │  └────────┬──────────┘  │
                  │           │             │
                  └───────────┼─────────────┘
                              │ serial (mutex-protected)
                              ▼
                       ┌──────────────┐
                       │   10Micron   │
                       │    mount     │
                       └──────────────┘
```

Threading model:

- The **main thread** runs the state-collection loop: every `MOUNT_CHECK_T` seconds it queries
mount status, coordinates, azimuth, pier side, weather, and updates the FITS header.
- The **command socket** is served by `sl_sock` which spawns one thread per connected client. All
command handlers run in those threads; access to the serial device is serialised by `mntdev_mutex`.
- The **Stellarium socket** spawns one thread per connected client, with each thread performing a
bidirectional exchange with the client.
- Shared state (`HDR`, mount status, input coordinates) is protected implicitly — all updates go
through accessor functions, and the serial port is guarded by `mntdev_mutex`.

Synchronisation primitives:

- `pthread_mutex_t mntdev_mutex` — serialises access to the mount device.
- `atomic_int mountstatus` — cached mount status visible from any thread.
- `atomic_int emul_status` — emulation-mode status.

---

## Known limitations

- The daemon assumes a well-behaved serial link; there is no protocol-level retry for corrupted
responses beyond 3 immediate retries on `mount_status()`.
- Weather data is read via `libweather`; the format is expected to be stable. If `libweather` is
unavailable at build time, the code will not build.
- Emulation mode does not model all mount statuses and does not implement true dual-axis motion; it
is a kinematic approximation suitable for client development.
- `mount_corrdata()` uses **local** time for the `:SLDT` command, which matches the 10Micron
firmware convention; this is intentional and differs from the rest of the daemon, which works in
UTC.
- The Stellarium protocol implementation uses JNow coordinates (the DAEMON does not perform the
JNow→J2000 conversion that some clients expect). Planetarium programs that assume J2000 output may
show a small offset; this is acceptable for visual pointing but should be considered if used for
astrometric work.

---

## License

All source files are licensed under **GNU General Public License v3.0** unless stated otherwise...

