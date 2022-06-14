/*
 * This file is part of the SSII project.
 * Copyright 2022 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#include <signal.h>         // signal
#include <stdio.h>          // printf
#include <stdlib.h>         // exit, free
#include <string.h>         // strdup
#include <unistd.h>         // sleep
#include <usefull_macros.h>

#include "cmdlnopts.h"
#include "sidservo.h"

/*
#include <termios.h>		// tcsetattr
#include <unistd.h>			// tcsetattr, close, read, write
#include <sys/ioctl.h>		// ioctl
#include <stdio.h>			// printf, getchar, fopen, perror
#include <stdlib.h>			// exit
#include <sys/stat.h>		// read
#include <fcntl.h>			// read
#include <signal.h>			// signal
#include <time.h>			// time
#include <string.h>			// memcpy
#include <stdint.h>			// int types
#include <sys/time.h>		// gettimeofday
*/

/**
 * This is an example of usage:
 *  - command line arguments,
 *  - log file,
 *  - check of another file version running,
 *  - signals management,
 *  - serial port reading/writing.
 * The `cmdlnopts.[hc]` are intrinsic files of this demo.
 */

static glob_pars *GP = NULL;  // for GP->pidfile need in `signals`

/**
 * We REDEFINE the default WEAK function of signal processing
 */
void signals(int sig){
    if(sig > 0){
        signal(sig, SIG_IGN);
        DBG("Get signal %d, quit.\n", sig);
    }
    LOGERR("Exit with status %d", sig);
    if(GP && GP->pidfile) // remove unnesessary PID file
        unlink(GP->pidfile);
    SSclose();
    exit(sig);
}

void iffound_default(pid_t pid){
    ERRX("Another copy of this process found, pid=%d. Exit.", pid);
}

int main(int argc, char *argv[]){
    initial_setup();
    char *self = strdup(argv[0]);
    GP = parse_args(argc, argv);
/*    if(GP->rest_pars_num){
        printf("%d extra options:\n", GP->rest_pars_num);
        for(int i = 0; i < GP->rest_pars_num; ++i)
            printf("%s\n", GP->rest_pars[i]);
    }*/
    check4running(self, GP->pidfile);
    red("%s started, snippets library version is %s\n", self, sl_libversion());
    free(self);
    signal(SIGTERM, signals); // kill (-15) - quit
    signal(SIGHUP, SIG_IGN);  // hup - ignore
    signal(SIGINT, signals);  // ctrl+C - quit
    signal(SIGQUIT, signals); // ctrl+\ - quit
    signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z
    if(GP->logfile) OPENLOG(GP->logfile, LOGLEVEL_ANY, 1);
    LOGMSG("Start application...");
    if(!SSinit(GP->device, GP->speed)) signals(-2);
    /*
    double t0 = dtime();
    while(1){ // read data from port and print in into terminal
        ;
    }*/
    // clean everything
    signals(0);
    // never reached
    return 0;
}
