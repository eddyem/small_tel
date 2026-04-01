/*
 * This file is part of the baader_dome project.
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
#include <linux/prctl.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <usefull_macros.h>

#include "header.h"
#include "socket.h"
#include "term.h"

#define DEFAULT_PIDFILE     "/tmp/domedaemon.pid"
#define DEFAULT_HEADERFILE  "/tmp/dome.fits"
#define DEFAULT_DOMENAME    "Baader"

static pid_t childpid = 0;

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
    char *headerfile;
    char *dome_name;
} parameters;

static parameters G = {
    .maxclients = 2,
    .serspeed = 9600,
    .sertmout = 5000,
    .pidfile = DEFAULT_PIDFILE,
    .headerfile = DEFAULT_HEADERFILE,
    .dome_name = DEFAULT_DOMENAME,
};

static sl_option_t cmdlnopts[] = {
    {"help",        NO_ARGS,    NULL,   'h',    arg_int,    APTR(&G.help),      "show this help"},
    {"verbose",     NO_ARGS,    NULL,   'v',    arg_none,   APTR(&G.verbose),   "verbose level (each -v adds 1)"},
    {"logfile",     NEED_ARG,   NULL,   'l',    arg_string, APTR(&G.logfile),   "log file name"},
    {"node",        NEED_ARG,   NULL,   'n',    arg_string, APTR(&G.node),      "node \"IP\", \"name:IP\" or path (could be \"\\0path\" for anonymous UNIX-socket)"},
    {"unixsock",    NO_ARGS,    NULL,   'u',    arg_int,    APTR(&G.isunix),    "UNIX socket instead of INET"},
    {"maxclients",  NEED_ARG,   NULL,   'm',    arg_int,    APTR(&G.maxclients),"max amount of clients connected to server (default: 2)"},
    {"pidfile",     NEED_ARG,   NULL,   'p',    arg_string, APTR(&G.pidfile),   "full path to PID-file (default: " DEFAULT_PIDFILE ")"},
    {"serialdev",   NEED_ARG,   NULL,   'd',    arg_string, APTR(&G.termpath),  "full path to serial device"},
    {"baudrate",    NEED_ARG,   NULL,   'b',    arg_int,    APTR(&G.serspeed),  "serial device speed (baud, default: 9600)"},
    {"sertmout",    NEED_ARG,   NULL,   'T',    arg_double, APTR(&G.sertmout),  "serial device timeout (us, default: 5000)"},
    {"headerfile",  NEED_ARG,   NULL,   'H',    arg_string, APTR(&G.headerfile),"full path to output FITS-header (default: " DEFAULT_HEADERFILE ")"},
    {"domename",    NEED_ARG,   NULL,   'N',    arg_string, APTR(&G.dome_name), "dome name in FITS-header (default: " DEFAULT_DOMENAME ")"},
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
        term_close();
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
    if(!G.node) ERRX("Point communication node");
    if(!G.termpath) ERRX("Point serial device path");
    if(!header_create(G.headerfile))
        ERRX("Cannot write into '%s'", G.headerfile);
    domename(G.dome_name);
    sl_check4running((char*)__progname, G.pidfile);
    if(sl_daemonize()) ERR("Can't daemonize!");
    sl_loglevel_e lvl = G.verbose + LOGLEVEL_ERR;
    if(lvl >= LOGLEVEL_AMOUNT) lvl = LOGLEVEL_AMOUNT - 1;
    if(G.logfile) OPENLOG(G.logfile, lvl, 1);
    LOGMSG("Started");
    if(!term_open(G.termpath, G.serspeed, G.sertmout)){
        LOGERR("Can't open %s", G.termpath);
        ERRX("Fatal error");
    }
    signal(SIGTERM, signals);
    signal(SIGINT, signals);
    signal(SIGQUIT, signals);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);
    while(1){ // guard for dead processes
        childpid = fork();
        if(childpid){
            LOGMSG("create child with PID %d\n", childpid);
            DBG("Created child with PID %d\n", childpid);
            wait(NULL);
            WARNX("Child %d died\n", childpid);
            LOGWARN("Child %d died\n", childpid);
            sleep(1);
        }else{
            prctl(PR_SET_PDEATHSIG, SIGTERM); // send SIGTERM to child when parent dies
            break; // go out to normal functional
        }
    }
    // react for USRx only in child
    signal(SIGUSR1, signals);
    signal(SIGUSR2, signals);
    runserver(G.isunix, G.node, G.maxclients);
    LOGERR("Server error -> exit");
    return 0;
}
