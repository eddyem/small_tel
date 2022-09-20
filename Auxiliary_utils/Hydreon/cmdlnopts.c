/*                                                                                                  geany_encoding=koi8-r
 * cmdlnopts.c - the only function that parse cmdln args and returns glob parameters
 *
 * Copyright 2013 Edward V. Emelianoff <eddy@sao.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include "cmdlnopts.h"
#include "usefull_macros.h"

/*
 * here are global parameters initialisation
 */
static int help;
static glob_pars  G;

// default PID filename:
#define DEFAULT_PIDFILE "/tmp/testcmdlnopts.pid"
// default serial device
#define DEFAULT_DEV     "/dev/ttyUSB0"

//            DEFAULTS
// default global parameters
static glob_pars const Gdefault = {
    .device = DEFAULT_DEV,
    .pidfile = DEFAULT_PIDFILE,
    .timeout = 300, // 5 minute timeout
};

/*
 * Define command line options by filling structure:
 *  name        has_arg     flag    val     type        argptr              help
*/
static myoption cmdlnopts[] = {
// common options
    {"help",    NO_ARGS,    NULL,   'h',    arg_int,    APTR(&help),        _("show this help")},
    {"device",  NEED_ARG,   NULL,   'd',    arg_string, APTR(&G.device),    _("serial device name (default: )" DEFAULT_DEV ")")},
    {"logfile", NEED_ARG,   NULL,   'l',    arg_string, APTR(&G.logfile),   _("file to save logs")},
    {"outfile", NEED_ARG,   NULL,   'o',    arg_string, APTR(&G.outfile),   _("put output data table into this file (delete existing)")},
    {"pidfile", NEED_ARG,   NULL,   'p',    arg_string, APTR(&G.pidfile),   _("pidfile (default: " DEFAULT_PIDFILE ")")},
    {"timeout", NEED_ARG,   NULL,   't',    arg_int,    APTR(&G.timeout),   _("timeout (seconds) to die if no data on input (default: 5min)")},
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
    size_t hlen = 1024;
    char helpstring[1024], *hptr = helpstring;
    snprintf(hptr, hlen, "Usage: %%s [args]\n\n\tWhere args are:\n");
    // format of help: "Usage: progname [args]\n"
    change_helpstring(helpstring);
    // parse arguments
    parseargs(&argc, &argv, cmdlnopts);
    if(help) showhelp(-1, cmdlnopts);
    if(argc > 0){
        WARNX("Ignore %d unknown parameters: ");
        for (i = 0; i < argc; i++)
            WARNX("%s", argv[i]);
    }
    return &G;
}

