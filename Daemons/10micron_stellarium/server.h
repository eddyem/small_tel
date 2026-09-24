/*
 * This file is part of the mountdaemon_10micron project.
 * Copyright 2025 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#include <usefull_macros.h>

// default and max available time for "usleep"
#define DEFAULT_SLEEP_T     100
#define MAX_SLEEP_T         10000

// maximal amount of connected clients
#define DEFAULT_MAXCLIENTS  5

// status checking interval, seconds
#define MOUNT_CHECK_T       10.

typedef struct{
    int cmd_isunix;         // UNIX-socket instead of INET for `cmdnode`
    const char *stellport;  // port of stellarium server; could be "localhost:port" for local-only work
    const char *cmdnode;    // node of command socket
    int maxclients;         // maximal amount of clients connected
} server_sock_t;

bool server_check(server_sock_t *sockt);
void server_run();
void server_stop();
unsigned int server_getsleept();
bool server_setsleept(unsigned int t);
