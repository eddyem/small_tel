/*
 * This file is part of the weatherdaemon project.
 * Copyright 2021 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <usefull_macros.h>
#include "cmdlnopts.h"

/*
 * here are global parameters initialisation
 */
int help;
static glob_pars  G;

// default values for Gdefault & help
#define DEFAULT_PORT    "12345"
#define DEFAULT_PID		"/tmp/weatherdaemon.pid"

//            DEFAULTS
// default global parameters
glob_pars const Gdefault = {
    .device = NULL,
    .port = DEFAULT_PORT,
    .logfile = NULL,
    .verb = 0,
    .tty_speed = 9600,
    .rest_pars = NULL,
    .rest_pars_num = 0,
    .emul = 0,
    .pidfile = DEFAULT_PID
};

/*
 * Define command line options by filling structure:
 *  name        has_arg     flag    val     type        argptr              help
*/
myoption cmdlnopts[] = {
// common options
    {"help",    NO_ARGS,    NULL,   'h',    arg_int,    APTR(&help),        _("show this help")},
    {"device",  NEED_ARG,   NULL,   'd',    arg_string, APTR(&G.device),    _("serial device name (default: none)")},
    {"port",    NEED_ARG,   NULL,   'p',    arg_string, APTR(&G.port),      _("network port to connect (default: " DEFAULT_PORT ")")},
    {"logfile", NEED_ARG,   NULL,   'l',    arg_string, APTR(&G.logfile),   _("save logs to file (default: none)")},
    {"verb",    NO_ARGS,    NULL,   'v',    arg_none,   APTR(&G.verb),      _("logfile verbocity level (each -v increase it)")},
    {"baudrate",NEED_ARG,   NULL,   'b',    arg_int,    APTR(&G.tty_speed), _("serial terminal baudrate (default: 9600)")},
    {"emulation",NO_ARGS,   NULL,   'e',    arg_int,    APTR(&G.emul),      _("emulate serial device")},
    {"pidfile", NEED_ARG,   NULL,   'P',    arg_string, APTR(&G.pidfile),   _("pidfile name (default: " DEFAULT_PID ")")},
    end_option
};

/**
 * Parse command line options and return dynamically allocated structure
 *      to global parameters
 * @param argc - copy of argc from main
 * @param argv - copy of argv from main
 * @return allocated structure with global parameters
 */
glob_pars *parse_args(int argc, char **argv){
    int i;
    void *ptr;
    ptr = memcpy(&G, &Gdefault, sizeof(G)); assert(ptr);
    // format of help: "Usage: progname [args]\n"
    change_helpstring("Usage: %s [args]\n\n\tWhere args are:\n");
    // parse arguments
    parseargs(&argc, &argv, cmdlnopts);
    if(help) showhelp(-1, cmdlnopts);
    if(argc > 0){
        G.rest_pars_num = argc;
        G.rest_pars = calloc(argc, sizeof(char*));
        for (i = 0; i < argc; i++)
            G.rest_pars[i] = strdup(argv[i]);
    }
    return &G;
}

