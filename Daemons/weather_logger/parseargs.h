/*
 * This file is part of the meteologger project.
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
    int isunix;             // use UNIX-sockets instead of net
    int verb;               // verbocity level
    double req_interval;    // requests interval
    double net_timeout;     // network timeout
    char *bddir;            // directory to store all data
    char *logfile;          // logfile name
    char *node;             // node of server
    char *pidfile;          // pidfile name
} glob_pars;

glob_pars *parseargs(int *argc, char ***argv);
