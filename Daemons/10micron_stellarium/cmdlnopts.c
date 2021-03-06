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
#include "parseargs.h"
#include "usefull_macro.h"

/*
 * here are global parameters initialisation
 */
int help;
glob_pars  G;
glob_pars *GP = NULL;

#define DEFAULT_COMDEV  "/dev/ttyUSB0"
// port for connections
#define DEFAULT_PORT "10000"
#define DEFAULT_DBGPORT "10001"
// weather server port and name
#define DEFAULT_WSPORT (12345)
#define DEFAULT_WSNAME "robotel1.sao.ru"
// default PID filename:
#define DEFAULT_PIDFILE "/tmp/stellariumdaemon.pid"
// default file with headers
#define DEFAULT_FITSHDR "/tmp/10micron.fitsheader"

//            DEFAULTS
// default global parameters
glob_pars const Gdefault = {
    .device = DEFAULT_COMDEV,
    .port = DEFAULT_PORT,
    .dbgport = DEFAULT_DBGPORT,
    .pidfile = DEFAULT_PIDFILE,
    .crdsfile = DEFAULT_FITSHDR,
    .emulation = 0,
    .weathserver = DEFAULT_WSNAME,
    .weathport = DEFAULT_WSPORT,
    .logfile = NULL // don't save logs
};

/*
 * Define command line options by filling structure:
 *  name        has_arg     flag    val     type        argptr              help
*/
myoption cmdlnopts[] = {
// common options
    {"help",    NO_ARGS,    NULL,   'h',    arg_int,    APTR(&help),        _("show this help")},
    {"device",  NEED_ARG,   NULL,   'd',    arg_string, APTR(&G.device),    _("serial device name (default: " DEFAULT_COMDEV ")")},
    {"emulation",NO_ARGS,   NULL,   'e',    arg_int,    APTR(&G.emulation), _("run in emulation mode")},
    //{"hostname",NEED_ARG,   NULL,   'H',    arg_string, APTR(&G.hostname),  _("hostname to connect (default: localhost)")},
    {"logfile", NEED_ARG,   NULL,   'l',    arg_string, APTR(&G.logfile),   _("file to save logs")},
    {"hdrfile", NEED_ARG,   NULL,   'o',    arg_string, APTR(&G.crdsfile),  _("file to save FITS-header with coordinates and time")},
    {"pidfile", NEED_ARG,   NULL,   'P',    arg_string, APTR(&G.pidfile),   _("pidfile (default: " DEFAULT_PIDFILE ")")},
    {"port",    NEED_ARG,   NULL,   'p',    arg_string, APTR(&G.port),      _("port to connect (default: " DEFAULT_PORT ")")},
    {"dbgport", NEED_ARG,   NULL,   'D',    arg_string, APTR(&G.dbgport),   _("port to connect for debug console (default: " DEFAULT_DBGPORT ")")},
    {"wport",   NEED_ARG,   NULL,   'w',    arg_int,    APTR(&G.weathport), _("weather server port")},
    {"wname",   NEED_ARG,   NULL,   'W',    arg_string, APTR(&G.weathserver),_("weather server address")},
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
        G.rest_pars_num = argc;
        G.rest_pars = calloc(argc, sizeof(char*));
        for (i = 0; i < argc; i++)
            G.rest_pars[i] = strdup(argv[i]);
    }
    return &G;
}

