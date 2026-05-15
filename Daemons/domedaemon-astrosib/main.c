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

#include "header.h"
#include "server.h"

// TCP socket port
#define DEFAULT_PORT        "55555"
// baudrate - 9600
#define DEFAULT_SERSPEED    9600
// serial polling timeout - 100ms
#define DEFAULT_SERTMOUT    100000
#define DEFAULT_PIDFILE     "/tmp/domedaemon.pid"
#define DEFAULT_HEADERFILE  "/tmp/dome.fits"
#define DEFAULT_DOMENAME    "Astrosib"

static pid_t childpid = 0;
static sl_tty_t *serial = NULL;

typedef struct{
    int help;
    char *device;           // serial device name
    char *node;             // port to connect or UNIX socket name
    char *logfile;          // logfile name
    char *pidfile;          // PID-file path
    char *headerfile;       // path to FITS-header with dome parameters
    char *dome_name;        // name of dome
    int isunix;             // open UNIX-socket instead of TCP
    int verbose;            // verbose level
} parameters;


static parameters G = {
    .node = DEFAULT_PORT,
    .pidfile = DEFAULT_PIDFILE,
    .headerfile = DEFAULT_HEADERFILE,
    .dome_name = DEFAULT_DOMENAME,
};

static sl_option_t cmdlnopts[] = {
    {"help",    NO_ARGS,    NULL,   'h',    arg_int,    APTR(&G.help),      "show this help"},
    {"device",  NEED_ARG,   NULL,   'd',    arg_string, APTR(&G.device),    "serial device name"},
    {"node",    NEED_ARG,   NULL,   'n',    arg_string, APTR(&G.node),      "UNIX socket name or network port to connect (default: " DEFAULT_PORT ")"},
    {"logfile", NEED_ARG,   NULL,   'l',    arg_string, APTR(&G.logfile),   "save logs to file"},
    {"unix",    NO_ARGS,    NULL,   'u',    arg_int,    APTR(&G.isunix),    "open UNIX-socket instead of TCP"},
    {"verbose", NO_ARGS,    NULL,   'v',    arg_none,   APTR(&G.verbose),   "logging verbose level (each -v adds one)"},
    {"pidfile", NEED_ARG,   NULL,   'p',    arg_string, APTR(&G.pidfile),   "full path to PID-file (default: " DEFAULT_PIDFILE ")"},
    {"headerfile",NEED_ARG, NULL,   'H',    arg_string, APTR(&G.headerfile),"full path to output FITS-header (default: " DEFAULT_HEADERFILE ")"},
    {"domename",NEED_ARG,   NULL,   'N',    arg_string, APTR(&G.dome_name), "dome name in FITS-header (default: " DEFAULT_DOMENAME ")"},
    end_option
};

void sl_iffound_deflt(pid_t pid){
    WARNX("Another copy of this process found, pid=%d. Exit.", pid);
    exit(1); // don't run `signals` to protect foreign PID-file from removal
}

// SIGUSR1 - FORBID observations
// SIGUSR2 - allow
void signals(int sig){
    if(sig){
        if(signals != signal(sig, SIG_IGN)) exit(sig); // function called "as is", before sig registration
        if(childpid == 0){ // child -> test USR1/USR2
            LOGDBG("Child gotta signal %d", sig);
            if(sig == SIGUSR1){
                forbid_observations(1);
                LOGMSG("Got signal `observations forbidden`");
                signal(sig, signals);
                return;
            }else if(sig == SIGUSR2){
                forbid_observations(0);
                LOGMSG("Got signal `observations permitted`");
                signal(sig, signals);
                return;
            }
        }
        LOGDBG("Get signal %d, quit.\n", sig);
    }
    if(childpid == 0){
        DBG("Stop server");
        LOGMSG("Stop server");
        stopserver();
        DBG("Close terminal");
        LOGMSG("Close terminal");
        if(serial) sl_tty_close(&serial);
    }else{
        if(G.pidfile){
            LOGMSG("Unlink %s", G.pidfile);
            usleep(10000);
            unlink(G.pidfile);
        }
    }
    LOGERR("Exit with status %d", sig);
    exit(sig);
}

int main(int argc, char **argv){
    sl_init();
    sl_parseargs(&argc, &argv, cmdlnopts);
    if(G.help) sl_showhelp(-1, cmdlnopts);
    if(!G.node) ERRX("Point node");
    if(!G.device) ERRX("Point path to serial device");
    if(!header_create(G.headerfile))
        ERRX("Cannot write into '%s'", G.headerfile);
    domename(G.dome_name);
    sl_check4running((char*)__progname, G.pidfile);
#ifndef EBUG
    if(sl_daemonize()) ERR("Can't daemonize!");
#endif
    sl_loglevel_e lvl = G.verbose + LOGLEVEL_ERR;
    if(lvl >= LOGLEVEL_AMOUNT) lvl = LOGLEVEL_AMOUNT - 1;
    if(G.logfile) OPENLOG(G.logfile, lvl, 1);
    LOGMSG("Started");
    signal(SIGTERM, signals);
    signal(SIGINT, signals);
    signal(SIGQUIT, signals);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGHUP, signals);
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);
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
    serial = sl_tty_new(G.device, DEFAULT_SERSPEED, 4096);
    if(serial) serial = sl_tty_open(serial, 1);
    if(!serial){
        LOGERR("Can't open serial device %s", G.device);
        ERRX("Can't open serial device %s", G.device);
    }
    signal(SIGUSR1, signals);
    signal(SIGUSR2, signals);
    sl_tty_tmout(DEFAULT_SERTMOUT);
    server_run(type, G.node, serial);
    LOGERR("Unreacheable code reached!");
    return 1;
}
