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

#include <linux/prctl.h>
#include <signal.h>
#include <stdatomic.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <usefull_macros.h>

#include "parseargs.h"
#include "server.h"

static volatile atomic_int catchsig = 0;
static pid_t childpid = 0;

void signals(int signo){
    signal(signo, SIG_IGN);
    if(catchsig == 0){
        DBG("superloop not inited");
        exit(signo);
    }
    if(0 == childpid){
        if(signo == SIGUSR1){ // reload logs after rotating
            DBG("Got USR1 -> reinit logs");
            if(!reinit_logs()){
                stop_server();
                catchsig = signo;
            }else signal(signo, signals);
        }else{ // kill
            DBG("Got %d -> kill", signo);
            stop_server();
            catchsig = signo; // could be 0 or error code / signo
        }
    }else{ // throw signal to child
        if(signo > 0){
            kill(childpid, signo);
            LOGMSG("Send received signal %d to child", signo);
            if(signo != SIGUSR1) catchsig = signo;
        }else catchsig = signo;
    }
}

static void prepare_and_run(glob_pars *G){
    if(!G) return;
    sl_socktype_e stype = (G->isunix) ? SOCKT_UNIX : SOCKT_NET;
    set_reqinterval(G->req_interval);
    set_nettimeout(G->net_timeout);
    DBG("Run server");
    run_server(G->node, stype, G->bddir);
    DBG("Server died");
}

int main(int argc, char **argv){
    sl_init();
    glob_pars *G = parseargs(&argc, &argv);
    if(!G) return 1;
    sl_check4running(NULL, G->pidfile);
    signal(SIGTERM, signals);
    signal(SIGINT, signals);
    signal(SIGQUIT, signals);
    signal(SIGPIPE, SIG_IGN); // for sockets
    signal(SIGUSR1, signals); // reload DB
#ifndef EBUG
    if(sl_daemonize()) ERRX("Can't daemonize");
    catchsig = INT_MAX; // now `signals` won't run exit()
    while(catchsig == INT_MAX){ // guard for dead processes
        childpid = fork();
        if(childpid){
            LOGDBG("create child with PID %d\n", childpid);
            DBG("Created child with PID %d\n", childpid);
            pid_t expid = 0;
            while(catchsig == INT_MAX){
                expid = waitpid(childpid, NULL, WNOHANG);
                if(expid < 0){
                    LOGERR("waitpid() returns -1; exit");
                    ERRX("waitpid() returns -1; exit");
                }
                if(expid == childpid) break;
                usleep(50000);
            }
            WARNX("Child %d died\n", childpid);
            LOGWARN("Child %d died\n", childpid);
            sleep(1);
        }else{
            prctl(PR_SET_PDEATHSIG, SIGTERM); // send SIGTERM to child when parent dies
            break; // go out to normal functional
        }
    }
#else
    // init for DEBUG mode
    catchsig = INT_MAX;
#endif

    if(childpid){
        LOGERR("Main process exits with status %d", catchsig);
        if(G->pidfile) unlink(G->pidfile);
    }else{
        pid_t self = getpid();
        prepare_and_run(G);
        if(catchsig == INT_MAX) LOGERR("Child process %d died", self);
        else LOGERR("Child process %d exits with status %d", self, catchsig);
        ; // cleanup child here
#ifdef EBUG
        if(G->pidfile) unlink(G->pidfile); // unlink PID-file in debug-mode
#endif
        usleep(10000); // wait processes to die
    }
    return catchsig;
}
