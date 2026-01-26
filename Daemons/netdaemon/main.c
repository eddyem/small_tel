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
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h> // wait
#include <sys/prctl.h> //prctl
#include <usefull_macros.h>
#include "cmdlnopts.h"
#include "socket.h"
#include "term.h"

glob_pars *GP;

void signals(int signo){
    sl_restore_con();
    if(ttydescr) sl_tty_close(&ttydescr);
    LOGERR("exit with status %d", signo);
    exit(signo);
}

int main(int argc, char **argv){
    sl_init();
    signal(SIGTERM, signals); // kill (-15) - quit
    signal(SIGHUP, SIG_IGN);  // hup - ignore
    signal(SIGINT, signals);  // ctrl+C - quit
    signal(SIGQUIT, signals); // ctrl+\ - quit
    signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z
    GP = parse_args(argc, argv);
    if(GP->terminal){
        if(!GP->device) ERRX(_("Point serial device name"));
        if(!try_connect(GP->device, GP->tty_speed))
            ERRX("Can't connect to device");
        run_terminal();
        signals(0); // never reached!
    }
    if(GP->logfile){
        sl_loglevel_e lvl = LOGLEVEL_ERR;
        for(; GP->verb && lvl < LOGLEVEL_ANY; --GP->verb) ++lvl;
        DBG("Loglevel: %d", lvl);
        if(!OPENLOG(GP->logfile, lvl, 1)) ERRX("Can't open log file");
        LOGERR("Started");
    }
    #ifndef EBUG
    if(daemon(1, 0)){
        ERR("daemon()");
    }
    while(1){ // guard for dead processes
        pid_t childpid = fork();
        if(childpid){
            LOGDBG("create child with PID %d\n", childpid);
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
    #endif

    if(GP->device) if(!try_connect(GP->device, GP->tty_speed))
        ERRX("Can't connect to device");;
    /*
     * INSERT CODE HERE
     * connection check & device validation
     */
    daemonize(GP->port);
    return 0;
}
