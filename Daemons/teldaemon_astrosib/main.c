/*
 * This file is part of the teldaemon project.
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

#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <usefull_macros.h>

#include "socket.h"
#include "term.h"

typedef struct{
    int help;
    int verbose;
    int isunix;
    int maxclients;
    int serspeed;
    double sertmout;
    char *logfile;
    char *node;
    char *termpath;
    char *pidfile;
} parameters;

static parameters G = {
    .maxclients = 2,
    .serspeed = 9600
};

static sl_option_t cmdlnopts[] = {
    {"help",        NO_ARGS,    NULL,   'h',    arg_int,    APTR(&G.help),      "show this help"},
    {"verbose",     NO_ARGS,    NULL,   'v',    arg_none,   APTR(&G.verbose),   "verbose level (each -v adds 1)"},
    {"logfile",     NEED_ARG,   NULL,   'l',    arg_string, APTR(&G.logfile),   "log file name"},
    {"node",        NEED_ARG,   NULL,   'n',    arg_string, APTR(&G.node),      "node \"IP\", \"name:IP\" or path (could be \"\\0path\" for anonymous UNIX-socket)"},
    {"unixsock",    NO_ARGS,    NULL,   'u',    arg_int,    APTR(&G.isunix),    "UNIX socket instead of INET"},
    {"maxclients",  NEED_ARG,   NULL,   'm',    arg_int,    APTR(&G.maxclients),"max amount of clients connected to server (default: 2)"},
    {"pidfile",     NEED_ARG,   NULL,   'p',    arg_string, APTR(&G.pidfile),   "PID-file"},
    {"serialdev",   NEED_ARG,   NULL,   'd',    arg_string, APTR(&G.termpath),  "full path to serial device"},
    {"baudrate",    NEED_ARG,   NULL,   'b',    arg_int,    APTR(&G.serspeed),  "serial device speed (baud)"},
    {"sertmout",    NEED_ARG,   NULL,   'T',    arg_double, APTR(&G.sertmout),  "serial device timeout (us)"},
    end_option
};


// SIGUSR1 - FORBID observations
// SIGUSR2 - allow
void signals(int sig){
    if(sig){
        if(sig == SIGUSR1){
            forbid_observations(1);
            return;
        }else if(sig == SIGUSR2){
            forbid_observations(0);
            return;
        }
        signal(sig, SIG_IGN);
        DBG("Get signal %d, quit.\n", sig);
        LOGERR("Exit with status %d", sig);
    }else LOGERR("Exit");
    DBG("Stop server");
    stopserver();
    DBG("Close terminal");
    close_term();
    DBG("Exit");
    exit(sig);
}


int main(int argc, char **argv){
    sl_init();
    sl_parseargs(&argc, &argv, cmdlnopts);
    if(G.help) sl_showhelp(-1, cmdlnopts);
    if(!G.node) ERRX("Point node");
    if(!G.termpath) ERRX("Point serial device path");
    sl_check4running((char*)__progname, G.pidfile);
    sl_loglevel_e lvl = G.verbose + LOGLEVEL_ERR;
    if(lvl >= LOGLEVEL_AMOUNT) lvl = LOGLEVEL_AMOUNT - 1;
    if(G.logfile) OPENLOG(G.logfile, lvl, 1);
    LOGMSG("Started");
    if(!open_term(G.termpath, G.serspeed, G.sertmout)){
        LOGERR("Can't open %s", G.termpath);
        ERRX("Fatal error");
    }
    signal(SIGTERM, signals);
    signal(SIGINT, signals);
    signal(SIGQUIT, signals);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGHUP, signals);
    signal(SIGUSR1, signals);
    signal(SIGUSR2, signals);
    runserver(G.isunix, G.node, G.maxclients);
    LOGMSG("Ended");
    DBG("Close");
    return 0;
}
