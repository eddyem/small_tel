/*
 * This file is part of the Hydreon_RG11 project.
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

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <usefull_macros.h>

#include "cmdlnopts.h"
#include "hydreon.h"

static glob_pars *G = NULL;

void signals(int sig){
    if(sig > 0){
        signal(sig, SIG_IGN);
        DBG("Get signal %d, quit.\n", sig);
    }
    LOGERR("Exit with status %d", sig);
    if(G && G->pidfile) // remove unnesessary PID file
        unlink(G->pidfile);
    hydreon_close();
    exit(sig);
}

static void dumpRchanges(rg11 *new, rg11 *old){
    DBG("Regular changed");
    uint8_t *n = (uint8_t*) new, *o = (uint8_t*) old;
    int start = 1;
    for(int i = 0; i < RREGNUM; ++i){
        if(o[i] != n[i]){
            sl_putlogt(start, globlog, LOGLEVEL_MSG, "%s=%d", regname(i), n[i]);
            DBG("%s=%d", regname(i), n[i]);
            if(start) start = 0;
        }
    }
    uint8_t xOr = new->RGBits ^ old->RGBits;
    start = 1;
    if(xOr){
        uint8_t f = 1;
        for(int i = 0; i < RGBITNUM; ++i, f <<= 1){
            if(xOr & f){
                sl_putlogt(start, globlog, LOGLEVEL_MSG, "%s=%d", rgbitname(i), (new->RGBits & f) ? 1 : 0);
                DBG("%s=%d", rgbitname(i), (new->RGBits & f) ? 1 : 0);
                if(start) start = 0;
            }
        }
    }
}

static void dumpSchanges(slowregs *new, slowregs *old){
    DBG("Slow changed");
    uint8_t *n = (uint8_t*) new, *o = (uint8_t*) old;
    int start = 1;
    for(int i = 0; i < SREGNUM; ++i){
        if(o[i] != n[i]){
            sl_putlogt(start, globlog, LOGLEVEL_MSG, "%s=%d", slowname(i), n[i]);
            DBG("%s=%d", slowname(i), n[i]);
            if(start) start = 0;
        }
    }
}

int main(int argc, char **argv){
    initial_setup();
    char *self = strdup(argv[0]);
    G = parse_args(argc, argv);
    if(G->timeout < 5) ERRX("Timeout should be not less than 5 seconds");
    if(!G->logfile) ERRX("Point log file name");
    check4running(self, G->pidfile);
    if(!hydreon_open(G->device)) return 1;
    if(G->logfile) OPENLOG(G->logfile, LOGLEVEL_ANY, 0);
    rg11 Rregs, oRregs = {0};
    slowregs Sregs, oSregs = {0};
    signal(SIGTERM, signals); // kill (-15) - quit
    signal(SIGHUP, SIG_IGN);  // hup - ignore
    signal(SIGINT, signals);  // ctrl+C - quit
    signal(SIGQUIT, signals); // ctrl+\ - quit
    signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z
    double t0 = dtime();
    while(dtime() - t0 < (double)G->timeout){ // dump only changes
        if(!hydreon_getpacket(&Rregs, &Sregs)) continue;
        if(memcmp(&Rregs, &oRregs, RREGNUM + 1)){ // Rregs changed -> log changes
            dumpRchanges(&Rregs, &oRregs);
            memcpy(&oRregs, &Rregs, sizeof(rg11));
        }
        if(memcmp(&Sregs, &oSregs, sizeof(slowregs))){ // Sregs changed -> log
            dumpSchanges(&Sregs, &oSregs);
            memcpy(&oSregs, &Sregs, sizeof(slowregs));
        }
        t0 = dtime();
    }
    signals(-1); // never reached
    return 0;
}
