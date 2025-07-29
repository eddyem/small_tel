/*
 * This file is part of the libsidservo project.
 * Copyright 2025 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

// dump telescope moving using simplest goto command

#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <usefull_macros.h>

#include "conf.h"
#include "dump.h"
#include "sidservo.h"
#include "simpleconv.h"

typedef struct{
    int help;
    int verbose;
    int Ncycles;
    char *logfile;
    char *coordsoutput;
    char *conffile;
} parameters;

static parameters G = {
    .Ncycles = 40,
};
static FILE *fcoords = NULL;

static sl_option_t cmdlnopts[] = {
    {"help",        NO_ARGS,    NULL,   'h',    arg_int,    APTR(&G.help),      "show this help"},
    {"verbose",     NO_ARGS,    NULL,   'v',    arg_none,   APTR(&G.verbose),   "verbose level (each -v adds 1)"},
    {"logfile",     NEED_ARG,   NULL,   'l',    arg_string, APTR(&G.logfile),   "log file name"},
    {"ncycles",     NEED_ARG,   NULL,   'n',    arg_int,    APTR(&G.Ncycles),   "N cycles in stopped state (default: 40)"},
    {"coordsfile",  NEED_ARG,   NULL,   'o',    arg_string, APTR(&G.coordsoutput),"output file with coordinates log"},
    {"conffile",    NEED_ARG,   NULL,   'C',    arg_string, APTR(&G.conffile),  "configuration file name"},
    end_option
};

void signals(int sig){
    if(sig){
        signal(sig, SIG_IGN);
        DBG("Get signal %d, quit.\n", sig);
    }
    LOGERR("Exit with status %d", sig);
    Mount.quit();
    exit(sig);
}

int main(int argc, char **argv){
    sl_init();
    sl_parseargs(&argc, &argv, cmdlnopts);
    if(G.help) sl_showhelp(-1, cmdlnopts);
    sl_loglevel_e lvl = G.verbose + LOGLEVEL_ERR;
    if(lvl >= LOGLEVEL_AMOUNT) lvl = LOGLEVEL_AMOUNT - 1;
    if(G.logfile) OPENLOG(G.logfile, lvl, 1);
    conf_t *Config = readServoConf(G.conffile);
    if(!Config){
        dumpConf();
        return 1;
    }
    if(G.coordsoutput){
        if(!(fcoords = fopen(G.coordsoutput, "w")))
            ERRX("Can't open %s", G.coordsoutput);
    }else fcoords = stdout;
    logmnt(fcoords, NULL);
    time_t curtime = time(NULL);
    LOGMSG("Started @ %s", ctime(&curtime));
    LOGMSG("Mount device %s @ %d", Config->MountDevPath, Config->MountDevSpeed);
    LOGMSG("Encoder device %s @ %d", Config->EncoderDevPath, Config->EncoderDevSpeed);
    if(MCC_E_OK != Mount.init(Config)) ERRX("Can't init devices");
    coordval_pair_t M;
    if(!getPos(&M, NULL)) ERRX("Can't get current position");
    signal(SIGTERM, signals); // kill (-15) - quit
    signal(SIGHUP, SIG_IGN);  // hup - ignore
    signal(SIGINT, signals);  // ctrl+C - quit
    signal(SIGQUIT, signals); // ctrl+\ - quit
    signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z
    coordpair_t tag = {.X = DEG2RAD(45.) + M.X.val, .Y = DEG2RAD(45.) + M.Y.val};
    if(MCC_E_OK != Mount.moveTo(&tag))
        ERRX("Can't move to 45, 45");
    dumpmoving(fcoords, 30., G.Ncycles);
    tag.X = M.X.val; tag.Y = M.Y.val;
    Mount.moveTo(&tag);
    dumpmoving(fcoords, 30., G.Ncycles);
    signals(0);
    return 0;
}
