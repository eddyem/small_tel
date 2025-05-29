/*
 * This file is part of the Snippets project.
 * Copyright 2024 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h> // wait
#include <sys/prctl.h> //prctl
#include <usefull_macros.h>

#include "server.h"

// TCP socket port
#define DEFAULT_PORT        "55555"
// baudrate - 9600
#define DEFAULT_SERSPEED    9600
// serial polling timeout - 100ms
#define DEFAULT_SERTMOUT    100000

typedef struct{
    char *device;           // serial device name
    char *node;             // port to connect or UNIX socket name
    char *logfile;          // logfile name
    int isunix;             // open UNIX-socket instead of TCP
    int verbose;            // verbose level
} parameters;

static parameters G = {
    .node = DEFAULT_PORT,
};
static int help;

static sl_option_t cmdlnopts[] = {
    {"help",    NO_ARGS,    NULL,   'h',    arg_int,    APTR(&help),        "show this help"},
    {"device",  NEED_ARG,   NULL,   'd',    arg_string, APTR(&G.device),    "serial device name"},
    {"node",    NEED_ARG,   NULL,   'n',    arg_string, APTR(&G.node),      "UNIX socket name or network port to connect (default: " DEFAULT_PORT ")"},
    {"logfile", NEED_ARG,   NULL,   'l',    arg_string, APTR(&G.logfile),   "save logs to file"},
    {"unix",    NO_ARGS,    NULL,   'u',    arg_int,    APTR(&G.isunix),    "open UNIX-socket instead of TCP"},
    {"verbose", NO_ARGS,    NULL,   'v',    arg_none,   APTR(&G.verbose),   "logging verbose level (each -v adds one)"},
    end_option
};

void signals(int sig){
    if(sig){
        signal(sig, SIG_IGN);
        DBG("Get signal %d, quit.\n", sig);
        LOGERR("Exit with status %d", sig);
    }else LOGERR("Exit");
    exit(sig);
}

int main(int argc, char **argv){
    sl_init();
    sl_parseargs(&argc, &argv, cmdlnopts);
    if(help) sl_showhelp(-1, cmdlnopts);
    if(!G.node) ERRX("Point node");
    if(!G.device) ERRX("Point path to serial device");
    sl_loglevel_e lvl = G.verbose + LOGLEVEL_ERR;
    if(lvl >= LOGLEVEL_AMOUNT) lvl = LOGLEVEL_AMOUNT - 1;
    if(G.logfile) OPENLOG(G.logfile, lvl, 1);
    LOGMSG("Started");
    signal(SIGTERM, signals);
    signal(SIGINT, signals);
    signal(SIGQUIT, signals);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGHUP, signals);
#ifndef EBUG
    time_t lastd = 0;
    while(1){ // guard for dead processes
        pid_t childpid = fork();
        if(childpid){
            DBG("Created child with PID %d\n", childpid);
            LOGMSG("Created child with PID %d\n", childpid);
            wait(NULL);
            time_t t = time(NULL);
            if(t - lastd > 600){ // at least 10 minutes of work
                LOGERR("Child %d died\n", childpid);
            }
            lastd = t;
            WARNX("Child %d died\n", childpid);
            sleep(1);
        }else{
            prctl(PR_SET_PDEATHSIG, SIGTERM); // send SIGTERM to child when parent dies
            break; // go out to normal functional
        }
    }
#endif
    sl_socktype_e type = (G.isunix) ? SOCKT_UNIX : SOCKT_NETLOCAL;
    sl_tty_t *serial = sl_tty_new(G.device, DEFAULT_SERSPEED, 4096);
    if(serial) serial = sl_tty_open(serial, 1);
    if(!serial){
        LOGERR("Can't open serial device %s", G.device);
        ERRX("Can't open serial device %s", G.device);
    }
    sl_tty_tmout(DEFAULT_SERTMOUT);
    server_run(type, G.node, serial);
    LOGERR("Unreacheable code reached!");
    return 0;
}
