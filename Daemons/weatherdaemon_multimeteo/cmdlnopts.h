/*
 * This file is part of the weatherdaemon project.
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

/*
 * here are some typedef's for global data
 */
typedef struct{
    char *sockname;         // UNIX socket name for internal connections (commands etc)
    char *port;             // port for external clients
    char *logfile;          // logfile name
    int verb;               // verbocity level
    char *pidfile;          // pidfile name
    char **plugins;         // all plugins connected
    int nplugins;           // amount of plugins
    char *conffile;         // configuration file used instead of long command line
} glob_pars;


glob_pars *parse_args(int argc, char **argv);
