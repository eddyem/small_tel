/*
 * This file is part of the mountdaemon_10micron project.
 * Copyright 2026 Edward V. Emelianov <edward.emelianoff@gmail.com>.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

typedef struct{
    char *device;           // serial device name
    char *port;             // port to connect
    char *cmdnode;          // port for commands sending
    char *pidfile;          // name of PID file
    char *logfile;          // logging to this file
    char *crdsfile;         // file where FITS-header should be written
    char *mountname;        // set mount name for FITS-header
    int emulation;          // run in emulation mode
    int verbose;            // verbose level
    int sleept;             // server's sleeping in main cycle time
    int isunix;             // use UNIX-socket for command port
    int sertmout;           // serial timeout, us
    int serspeed;           // serial speed, baud
    int maxclients;         // max amount of clients connected to one socket
} parameters_t;

parameters_t *parse_cmdline(int *argc, char ***argv);
