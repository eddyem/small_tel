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

#include <string.h>
#include <usefull_macros.h>

#include "parseargs.h"
#include "server.h"

#define DEFAULT_PID         "/tmp/meteologger.pid"
#define MIN_REQ_INTERVAL    (0.2)
#define MAX_REQ_INTERVAL    (900.)
#define MIN_NET_TMOUT       (0.1)
#define MAX_NET_TMOUT       (30.)

static int help;

static glob_pars G = {
    .pidfile = DEFAULT_PID,
    .req_interval = 0.5,
    .net_timeout = 1.,
};

static sl_option_t opts[] = {
    {"help",    NO_ARGS,    NULL,   'h',    arg_int,    APTR(&help),        "show this help"},
    {"node",    NEED_ARG,   NULL,   'n',    arg_string, APTR(&G.node),      "node to connect (host:port or UNIX socket name)"},
    {"logfile", NEED_ARG,   NULL,   'l',    arg_string, APTR(&G.logfile),   "save logs to file"},
    {"pidfile", NEED_ARG,   NULL,   'p',    arg_string, APTR(&G.pidfile),   "pidfile name (default: " DEFAULT_PID ")"},
    {"isunix",  NO_ARGS,    NULL,   'u',    arg_string, APTR(&G.isunix),    "use UNIX socket instead of network"},
    {"verbose", NO_ARGS,    NULL,   'v',    arg_none,   APTR(&G.verb),      "verbose level (each -v increases)"},
    {"output",  NEED_ARG,   NULL,   'o',    arg_string, APTR(&G.bddir),     "output directory to store database"},
    {"interval",NEED_ARG,   NULL,   'i',    arg_double, APTR(&G.req_interval), "weather request interval, s (min: 0.2, max: 900)"},
    {"timeout", NEED_ARG,   NULL,   't',    arg_double, APTR(&G.net_timeout),"timeout for server's answer, s (min: 0.1, max: 30)"},
    end_option
};

glob_pars *parseargs(int *argc, char ***argv){
    sl_parseargs(argc, argv, opts);
    if(help){
        sl_showhelp(-1, opts);
        return NULL;
    }
    if(!G.node) ERRX("Point node to correct");
    if(!G.bddir) ERRX("Need to know path to save DB");
    if(!checkDBpath(G.bddir)) ERRX("Can't create logs in %s", G.bddir);
    // remove trailing '/'
    int eol = strlen(G.bddir) - 1;
    DBG("eol=%d", eol);
    while(eol > 0){
        DBG("before: %s", G.bddir);
        if(G.bddir[eol] == '/') G.bddir[eol] = 0;
        else break;
        DBG("after: %s", G.bddir);
        --eol;
    }
    if(G.req_interval < MIN_REQ_INTERVAL || G.req_interval > MAX_REQ_INTERVAL)
        ERRX("Wrong time interval %g, should be in [%g, %g]", G.req_interval, MIN_REQ_INTERVAL, MAX_REQ_INTERVAL);
    if(G.net_timeout < MIN_NET_TMOUT || G.net_timeout > MAX_NET_TMOUT)
        ERRX("Wrong network timeout %g, should be in [%g, %g]", G.net_timeout, MIN_NET_TMOUT, MAX_NET_TMOUT);
    if(G.logfile){
        sl_loglevel_e lvl = LOGLEVEL_ERR + G.verb;
        if(lvl >= LOGLEVEL_AMOUNT) lvl = LOGLEVEL_AMOUNT - 1;
        DBG("Loglevel: %d", lvl);
        if(!OPENLOG(G.logfile, lvl, 1)) ERRX("Can't open log file %s", G.logfile);
    }
    return &G;
}
