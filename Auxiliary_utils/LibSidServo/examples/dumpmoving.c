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

#include "dump.h"
#include "sidservo.h"
#include "simpleconv.h"

typedef struct{
    int help;
    int verbose;
    int Ncycles;
    char *logfile;
    char *coordsoutput;
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

static conf_t Config = {
    .MountDevPath = "/dev/ttyUSB0",
    .MountDevSpeed = 19200,
    //.EncoderDevPath = "/dev/ttyUSB1",
    //.EncoderDevSpeed = 153000,
    .MountReqInterval = 0.1,
    .SepEncoder = 0
};

int main(int argc, char **argv){
    sl_init();
    sl_parseargs(&argc, &argv, cmdlnopts);
    if(G.help) sl_showhelp(-1, cmdlnopts);
    sl_loglevel_e lvl = G.verbose + LOGLEVEL_ERR;
    if(lvl >= LOGLEVEL_AMOUNT) lvl = LOGLEVEL_AMOUNT - 1;
    if(G.logfile) OPENLOG(G.logfile, lvl, 1);
    if(G.coordsoutput){
        if(!(fcoords = fopen(G.coordsoutput, "w")))
            ERRX("Can't open %s", G.coordsoutput);
    }else fcoords = stdout;
    logmnt(fcoords, NULL);
    time_t curtime = time(NULL);
    LOGMSG("Started @ %s", ctime(&curtime));
    LOGMSG("Mount device %s @ %d", Config.MountDevPath, Config.MountDevSpeed);
    LOGMSG("Encoder device %s @ %d", Config.EncoderDevPath, Config.EncoderDevSpeed);
    mcc_errcodes_t e = Mount.init(&Config);
    if(e != MCC_E_OK){
        WARNX("Can't init devices");
        return 1;
    }
    signal(SIGTERM, signals); // kill (-15) - quit
    signal(SIGHUP, SIG_IGN);  // hup - ignore
    signal(SIGINT, signals);  // ctrl+C - quit
    signal(SIGQUIT, signals); // ctrl+\ - quit
    signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z
    if(MCC_E_OK != Mount.moveTo(DEG2RAD(45.), DEG2RAD(45.)))
        ERRX("Can't move to 45, 45");
    dumpmoving(fcoords, 30., G.Ncycles);
    Mount.moveTo(0., 0.);
    dumpmoving(fcoords, 30., G.Ncycles);
    signals(0);
    return 0;
}
