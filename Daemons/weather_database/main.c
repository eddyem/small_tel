/*
 * This file is part of the sqlite project.
 * Copyright 2023 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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
#include <sqlite3.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <usefull_macros.h>

#include "socket.h"
#include "sql.h"

#define DEFAULT_PIDFILE     "/tmp/wdb"

static pid_t childpid = 0;

typedef struct{
    char *logfile;
    char *server;
    char *port;
    char *dbname;
    char *pidfile;
    int v;
    int help;
} opts_t;
static opts_t G = {.pidfile = DEFAULT_PIDFILE};

//void signals(int signo) __attribute__((noreturn));

void signals(int signo){
    if(childpid){ // slave process
        LOGWARN("Child killed with sig=%d", signo);
        closedb();
        exit(signo);
    }
    if(signo) LOGERR("Received signal %d, die", signo);
    unlink(G.pidfile);
    exit(signo);
}

static myoption cmdlnopts[] = {
    {"help",    NO_ARGS,    NULL,   'h',    arg_int,    APTR(&G.help),      "show this help"},
    {"address", NEED_ARG,   NULL,   'a',    arg_string, APTR(&G.server),    "server name or IP"},
    {"port",    NEED_ARG,   NULL,   'p',    arg_string, APTR(&G.port),      "server port"},
    {"logfile", NEED_ARG,   NULL,   'l',    arg_string, APTR(&G.logfile),   "logging file name"},
    {"verbose", NO_ARGS,    NULL,   'v',    arg_none,   APTR(&G.v),         "verbose level (each -v increases)"},
    {"pidfile", NEED_ARG,   NULL,   'P',    arg_string, APTR(&G.pidfile),   "pidfile (default: " DEFAULT_PIDFILE},
    {"database",NEED_ARG,   NULL,   'd',    arg_string, APTR(&G.dbname),    "database file"}
};

int main(int argc, char **argv){
    char *self = strdup(argv[0]);
    initial_setup();
    parseargs(&argc, &argv, cmdlnopts);
    if(G.help) showhelp(-1, cmdlnopts);
    if(argc > 0) WARNX("Got %d unused keys", argc);
    if(!G.dbname) ERRX("Point database file name");
    if(!G.server) ERRX("Point server IP or name");
    if(!G.port) ERRX("Point server port");
    sl_loglevel lvl = LOGLEVEL_ERR + G.v;
    if(lvl > LOGLEVEL_ANY) lvl = LOGLEVEL_ANY;
    if(G.logfile) OPENLOG(G.logfile, lvl, 1);
    LOGMSG("hello, start");
    LOGDBG("SQLite version: %s", sqlite3_libversion());
    check4running(self, G.pidfile);
    // signal reactions:
    signal(SIGTERM, signals); // kill (-15) - quit
    signal(SIGHUP, SIG_IGN);  // hup - ignore
    signal(SIGINT, signals);  // ctrl+C - quit
    signal(SIGQUIT, signals); // ctrl+\ - quit
    signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z
    LOGMSG("Started");
#ifndef EBUG
    unsigned int pause = 5;
    while(1){
        childpid = fork();
        if(childpid){ // master
            double t0 = dtime();
            LOGMSG("Created child with pid %d", childpid);
            wait(NULL);
            LOGWARN("Child %d died", childpid);
            if(dtime() - t0 < 1.) pause += 5;
            else pause = 1;
            if(pause > 900) pause = 900;
            sleep(pause); // wait a little before respawn
        }else{ // slave
            prctl(PR_SET_PDEATHSIG, SIGTERM); // send SIGTERM to child when parent dies
            break;
        }
    }
#endif
    if(!opendb(G.dbname)) return 1;
    int sock = open_socket(G.server, G.port);
    if(sock < 0) ERRX("Can't open socket to %s:%s", G.server, G.port);
    run_socket(sock);
    LOGERR("Unreachable code reached");
    signals(1);
    return 0;
}
