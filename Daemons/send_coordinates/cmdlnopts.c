/*
 * This file is part of the SendCoords project.
 * Copyright 2020 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cmdlnopts.h"
#include "usefull_macros.h"

/*
 * here are global parameters initialisation
 */
static int help;
static glob_pars  G;

#define DEFAULT_PIDFILE "/tmp/sendcoords.pid"
#define DEFAULT_PORT    "10000"
#define DEFAULT_HOST    "localhost"

//            DEFAULTS
// default global parameters
static glob_pars const Gdefault = {
    .pidfile = DEFAULT_PIDFILE,
    .port = DEFAULT_PORT,
    .host = DEFAULT_HOST,
};

/*
 * Define command line options by filling structure:
 *  name        has_arg     flag    val     type        argptr              help
*/
static myoption cmdlnopts[] = {
// common options
    {"help",    NO_ARGS,    NULL,   'h',    arg_int,    APTR(&help),        _("show this help")},
    {"pidfile", NEED_ARG,   NULL,   'p',    arg_string, APTR(&G.pidfile),   _("pidfile (default: " DEFAULT_PIDFILE ")")},
    {"port",    NEED_ARG,   NULL,   'P',    arg_string, APTR(&G.port),      _("port to connect (default: " DEFAULT_PORT ")")},
    {"host",    NEED_ARG,   NULL,   'H',    arg_string, APTR(&G.host),      _("host to connect (default: " DEFAULT_HOST ")")},
    {"ra",      NEED_ARG,   NULL,   'r',    arg_string, APTR(&G.ra),        _("target RA: HH:MM:SS.SS")},
    {"dec",     NEED_ARG,   NULL,   'd',    arg_string, APTR(&G.dec),       _("target DEC: [+-]DD:MM:SS.SS")},
    {"quiet",   NO_ARGS,    NULL,   'q',    arg_none,   APTR(&G.quiet),     _("suppress all messages to stdout")},
    {"monitor", NO_ARGS,    NULL,   'm',    arg_none,   APTR(&G.monitor),   _("monitor coordinates")},
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
        fprintf(stderr, "Undefined extra parameters!\n");
        showhelp(-1, cmdlnopts);
        G.rest_pars_num = argc;
        G.rest_pars = MALLOC(char *, argc);
        for (i = 0; i < argc; i++)
            G.rest_pars[i] = strdup(argv[i]);
    }
    return &G;
}

