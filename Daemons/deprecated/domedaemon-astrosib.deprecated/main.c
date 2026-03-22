/*                                                                                                  geany_encoding=koi8-r
 * main.c
 *
 * Copyright 2018 Edward V. Emelianov <eddy@sao.ru, edward.emelianoff@gmail.com>
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
#include "usefull_macros.h"
#include <signal.h>
#include <sys/wait.h> // wait
#include <sys/prctl.h> //prctl
#include <time.h>
#include "cmdlnopts.h"
#include "socket.h"

// dome @ /dev/ttyS2

glob_pars *GP;
static pid_t childpid = 0;

void signals(int signo){
    if(childpid){ // parent process
        restore_tty();
        putlog("exit with status %d", signo);
    }
    exit(signo);
}

int main(int argc, char **argv){
    initial_setup();
    signal(SIGTERM, signals); // kill (-15) - quit
    signal(SIGHUP, SIG_IGN);  // hup - ignore
    signal(SIGINT, signals);  // ctrl+C - quit
    signal(SIGQUIT, signals); // ctrl+\ - quit
    signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z
    GP = parse_args(argc, argv);
    if(GP->terminal){
        if(!GP->device) ERRX(_("Point serial device name"));
        try_connect(GP->device);
        run_terminal();
        signals(0); // never reached!
    }
    if(GP->logfile)
        openlogfile(GP->logfile);
    #ifndef EBUG
    if(daemon(1, 0)){
        ERR("daemon()");
    }
    time_t lastd = 0;
    while(1){ // guard for dead processes
        childpid = fork();
        if(childpid){
            DBG("Created child with PID %d\n", childpid);
            wait(NULL);
            time_t t = time(NULL);
            if(t - lastd > 600) // at least 10 minutes of work
                putlog("child %d died\n", childpid);
            lastd = t;
            WARNX("Child %d died\n", childpid);
            sleep(1);
        }else{
            prctl(PR_SET_PDEATHSIG, SIGTERM); // send SIGTERM to child when parent dies
            break; // go out to normal functional
        }
    }
    #endif

    if(GP->device) try_connect(GP->device);
    if(!poll_device()){
        ERRX(_("No answer from device"));
    }
    putlog("Child %d connected to %s", getpid(), GP->device);
    daemonize(GP->port);
    signals(0); // newer reached
    return 0;
}
