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

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h> // wait
#include <sys/prctl.h> //prctl
#include <usefull_macros.h>

#include "args.h"
#include "server.h"
#include "mount.h"

static pid_t childpid = -1; // PID of child process
static parameters_t *G = NULL;
#ifndef EBUG
static bool isrunning = false;
#endif

void signals(int sig){
    if(childpid){ // single or parent process
        if(G->pidfile) unlink(G->pidfile);  // remove pidfile
        if(childpid > 0) kill(childpid, SIGTERM);
    }
    if(sig){
        signal(sig, SIG_IGN);
        DBG("Get signal %d, quit.\n", sig);
    }
    if(childpid > 0){ // parent process
        LOGERR("PID %d exit with status %d", getpid(), sig);
    }else{
        LOGWARN("Child %d died with %d", getpid(), sig);
        server_stop();
    }
#ifndef EBUG
    isrunning = true;
#endif
    DBG("EXIT");
}

int main(int argc, char **argv){
    sl_init();
    G = parse_cmdline(&argc, &argv);
    if(!G) return 1;
    sl_loglevel_e lvl = G->verbose + LOGLEVEL_ERR;
    if(lvl >= LOGLEVEL_AMOUNT) lvl = LOGLEVEL_AMOUNT - 1;
    DBG("verb: %d, level: %d", G->verbose, lvl);
    int fd;
    if((fd = open(G->crdsfile, O_WRONLY | O_TRUNC | O_CREAT, 0644)) < 0){ // test FITS-header file for writing
        WARN(_("Can't open %s for writing"), G->crdsfile);
        return 1;
    }
    close(fd);
    if(G->sleept < 1){
        WARNX("Sleeping time sould be positive value (%d)", G->sleept);
        return 2;
    }
    if(!server_setsleept(G->sleept)){
        WARNX("Can't set sleep time to %d", G->sleept);
        return 3;
    }
    if(G->emulation) set_emulation_mode();
    else if(!mount_set_dev(G->device, G->serspeed, G->sertmout)){
        WARNX("Can't open device %s @ %d", G->device, G->serspeed);
        return 4;
    }
    if(!mount_set_name(G->mountname)){
        WARNX("Can't set mount name to %s", G->mountname);
        return 5;
    }
    signal(SIGTERM, signals);
    signal(SIGINT, signals);
    signal(SIGQUIT, signals);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGHUP, signals);
    sl_check4running((char*)__progname, G->pidfile);
    if(G->logfile) OPENLOG(G->logfile, lvl, 1);
    DBG("Started");
    LOGMSG("Started, master PID=%d", getpid());
#ifndef EBUG
    time_t lastd = 0;
    while(isrunning){ // guard for dead processes
        childpid = fork();
        if(childpid < 0){
            LOGERR("fork() returns error");
            sleep(5);
            continue;
        }
        if(childpid){
            DBG("Created child with PID %d\n", childpid);
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
    server_sock_t sockt = {
        .cmd_isunix = G->isunix,
        .cmdnode = G->cmdnode,
        .stellport = G->port,
        .maxclients = G->maxclients,
    };
    if(!server_check(&sockt)){
        LOGERR("Can't run servers");
        ERRX("Can't run servers");
    }
    DBG("Run server");
    server_run();
    DBG("Server died");
    return 0;
}
