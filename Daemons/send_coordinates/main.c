/*
 * This file is part of the SendCoords project.
 * Copyright 2020 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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
#include "stelldaemon.h"

glob_pars *GP = NULL;  // for GP->pidfile need in `signals`

/**
 * We REDEFINE the default WEAK function of signal processing
 */
void signals(int sig){
    if(sig){
        signal(sig, SIG_IGN);
        DBG("Get signal %d, quit.\n", sig);
    }
    restore_console();
    exit(sig);
}

void iffound_default(pid_t pid){
    ERRX("Another copy of this process found, pid=%d. Exit.", pid);
}

int main(int argc, char *argv[]){
    initial_setup();
    char *self = strdup(argv[0]);
    GP = parse_args(argc, argv);
    DBG("here");
    if(GP->rest_pars_num){
        printf("%d extra options:\n", GP->rest_pars_num);
        for(int i = 0; i < GP->rest_pars_num; ++i)
            printf("%s\n", GP->rest_pars[i]);
    }
    DBG("here");
    if((GP->ra && !GP->dec) || (!GP->ra && GP->dec))
        ERRX("You should point both coordinates");
    DBG("here");
    free(self);
    DBG("here");
    signal(SIGTERM, signals); // kill (-15) - quit
    signal(SIGHUP, SIG_IGN);  // hup - ignore
    signal(SIGINT, signals);  // ctrl+C - quit
    signal(SIGQUIT, signals); // ctrl+\ - quit
    signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z
    DBG("here");
    setup_con();
    /*
    if(GP->rest_pars_num){
        for(int i = 0; i < GP->rest_pars_num; ++i)
            printf("Extra argument: %s\n", GP->rest_pars[i]);
    }*/
    mk_connection();
    // clean everything
    signals(0);
    return 0;
}

